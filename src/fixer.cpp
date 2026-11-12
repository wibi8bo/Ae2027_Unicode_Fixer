// AE 2027 GBK -> UTF-8 host-side fixer
// Hooks dvacore::string::UTF8to16 and falls back to GB18030 decoding when the
// input bytes are not valid UTF-8. Unlike the old fixer this sits at the AE
// host boundary, so plugins whose entry point is "PluginMain" (VideoCopilot
// Saber / Element) are covered as well.

#include <dlfcn.h>
#include <iconv.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <sys/mman.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char *kTag = "[UTF8Fixer2027]";

static FILE *g_log = nullptr;

static void LogInit() {
  if (!g_log) g_log = fopen("/tmp/ae2027_fixer.log", "a");
}

static void Log(const char *fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fprintf(stderr, "%s %s\n", kTag, buf);
  LogInit();
  if (g_log) {
    fprintf(g_log, "%s %s\n", kTag, buf);
    fflush(g_log);
  }
}

// ---------------------------------------------------------------- utf8 utils

static bool IsValidUTF8(const unsigned char *p, size_t n) {
  size_t i = 0;
  while (i < n) {
    unsigned char c = p[i];
    if (c < 0x80) {
      i++;
      continue;
    }
    size_t need;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      need = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      need = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + need >= n) return false;
    for (size_t k = 1; k <= need; k++) {
      if ((p[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (need == 1 && cp < 0x80) return false;
    if (need == 2 && cp < 0x800) return false;
    if (need == 3 && cp < 0x10000) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += need + 1;
  }
  return true;
}

static bool HasCJK(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
      i++;
      continue;
    }
    uint32_t cp = 0;
    size_t need = 0;
    if ((c & 0xE0) == 0xC0) {
      need = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      need = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      need = 3;
      cp = c & 0x07;
    } else {
      i++;
      continue;
    }
    if (i + need >= s.size()) break;
    for (size_t k = 1; k <= need; k++)
      cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    // CJK / CJK punctuation / kana / hangul: only then is this really a
    // localized (GBK) string rather than some other legacy 8-bit encoding.
    if (cp >= 0x2E80) return true;
    i += need + 1;
  }
  return false;
}

static bool GB18030ToUTF8(const unsigned char *in, size_t n, std::string *out) {
  iconv_t cd = iconv_open("UTF-8", "GB18030");
  if (cd == (iconv_t)-1) return false;
  std::string result;
  result.resize(n * 4 + 8);
  char *src = (char *)in;
  size_t srcLeft = n;
  char *dst = &result[0];
  size_t dstLeft = result.size();
  size_t rc = iconv(cd, &src, &srcLeft, &dst, &dstLeft);
  iconv_close(cd);
  if (rc == (size_t)-1 || srcLeft != 0) return false;
  result.resize(result.size() - dstLeft);
  *out = result;
  return true;
}

// ------------------------------------------------------------- inline hook

static void *AllocExec(size_t n) {
  void *p = mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
  if (p != MAP_FAILED) return p;
  p = mmap(nullptr, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON,
           -1, 0);
  if (p != MAP_FAILED) return p;
  vm_address_t addr = 0;
  if (vm_allocate(mach_task_self(), &addr, n, VM_FLAGS_ANYWHERE) == KERN_SUCCESS) {
    if (vm_protect(mach_task_self(), addr, n, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE) == KERN_SUCCESS)
      return (void *)addr;
  }
  return nullptr;
}

static bool MakeWritable(void *addr, size_t len) {
  vm_address_t page = (vm_address_t)addr & ~(vm_page_size - 1);
  vm_address_t end =
      ((vm_address_t)addr + len + vm_page_size - 1) & ~(vm_page_size - 1);
  return vm_protect(mach_task_self(), page, end - page, FALSE,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS;
}

static void MakeExecutable(void *addr, size_t len) {
  vm_address_t page = (vm_address_t)addr & ~(vm_page_size - 1);
  vm_address_t end =
      ((vm_address_t)addr + len + vm_page_size - 1) & ~(vm_page_size - 1);
  vm_protect(mach_task_self(), page, end - page, FALSE,
             VM_PROT_READ | VM_PROT_EXECUTE);
  __builtin___clear_cache((char *)addr, (char *)addr + len);
}

// Trampoline: replay the 4 prologue instructions, then jump to orig+16.
static void *BuildTrampoline(void *target) {
  uint32_t *mem = (uint32_t *)AllocExec(64);
  if (!mem) return nullptr;
  uintptr_t back = (uintptr_t)target + 16;
  pthread_jit_write_protect_np(0);
  memcpy(mem, target, 16);
  mem[4] = 0x58000050u;  // ldr x16, #8
  mem[5] = 0xd61f0200u;  // br x16
  memcpy(&mem[6], &back, sizeof(back));
  pthread_jit_write_protect_np(1);
  MakeExecutable(mem, 64);
  return mem;
}

static void InstallHook(void *target, void *hook) {
  if (!MakeWritable(target, 16)) {
    Log("MakeWritable failed");
    return;
  }
  uint32_t patch[4];
  patch[0] = 0x58000050u;  // ldr x16, #8
  patch[1] = 0xd61f0200u;  // br x16
  memcpy(&patch[2], &hook, sizeof(hook));
  pthread_jit_write_protect_np(0);
  memcpy(target, patch, 16);
  pthread_jit_write_protect_np(1);
  MakeExecutable(target, 16);
  Log("patched %p", target);
}

// --------------------------------------------------------------- the hook

// dvacore::string::UTF16 is a libc++ std::basic_string<unsigned short> (24
// bytes). Returning a 24-byte struct makes the compiler emit the exact same
// sret ABI as the original function, so no manual register handling is needed.
struct UTF16Ret {
  void *a;
  void *b;
  void *c;
};

typedef UTF16Ret (*OrigFn)(const unsigned char *, size_t, int *, size_t *);
static OrigFn g_orig = nullptr;

// Returns converted UTF-8 buffer (and its size) or nullptr to keep the original.
extern "C" const unsigned char *PrepareConversion(const unsigned char *data,
                                                  size_t size,
                                                  size_t *outSize) {
  static int converted = 0;
  static int rejected = 0;
  if (!data || size == 0) return nullptr;
  if (getenv("AE27_PASSTHROUGH")) return nullptr;
  size_t usable = strnlen((const char *)data, size);
  if (usable == 0 || IsValidUTF8(data, usable)) return nullptr;
  static thread_local std::string conv;
  std::string tmp;
  if (!GB18030ToUTF8(data, usable, &tmp) || !HasCJK(tmp)) {
    if (rejected < 20) {
      rejected++;
      char hex[400] = {0};
      size_t shown = usable < 50 ? usable : 50;
      for (size_t i = 0; i < shown; i++)
        snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", data[i]);
      Log("keep non-utf8 as-is: len=%zu bytes=%s", usable, hex);
    }
    return nullptr;
  }
  conv.swap(tmp);
  *outSize = conv.size();
  if (converted < 40) {
    converted++;
    Log("converted GBK -> UTF-8: \"%s\"", conv.c_str());
  }
  return (const unsigned char *)conv.c_str();
}

// Same ABI as the original: x0=data x1=size x2=err* x3=len* x8=sret.
// The compiler generates the prologue/epilogue, so nothing can be corrupted.
extern "C" UTF16Ret UTF8to16Hook(const unsigned char *data, size_t size,
                                 int *err, size_t *len) {
  size_t outSize = 0;
  const unsigned char *conv = PrepareConversion(data, size, &outSize);
  if (conv) return g_orig(conv, outSize, err, len);
  return g_orig(data, size, err, len);
}

// --------------------------------------------------------------- bootstrap

static const char *kMangled =
    "_ZN7dvacore6string8UTF8to16ENSt3__117basic_string_viewIhNS1_11char_traitsIhEEEEPNS0_16UnicodeErrorCodeEPm";


// Cheap sanity check so we never patch an address that is obviously not code.
static bool LooksLikeFunction(const void *p) {
  if (!p || ((uintptr_t)p & 3)) return false;
  const uint32_t *w = (const uint32_t *)p;
  if (w[0] == 0x00000000 || w[0] == 0xffffffff) return false;
  if (w[0] == 0xd65f03c0) return false;  // bare "ret": nothing to hook
  return true;
}

static bool IsAE27() {
  char path[4096];
  uint32_t sz = sizeof(path);
  if (_NSGetExecutablePath(path, &sz) != 0) return false;
  return strstr(path, "After Effects 2027") != nullptr ||
         strstr(path, "After Effects 27.") != nullptr;
}

void UTF8FixerInstall() {
  LogInit();
  void *sym = dlsym(RTLD_DEFAULT, kMangled);
  if (!sym) {
    if (!IsAE27()) {
      Log("not AE 2027, skip; dvacore::string::UTF8to16 not found");
      return;
    }
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
      const char *name = _dyld_get_image_name(i);
      if (!name || !strstr(name, "dvacore.framework")) continue;
      const struct mach_header *hdr = _dyld_get_image_header(i);
      sym = (void *)((vm_address_t)hdr + 0x22e8a8);
      Log("dvacore image %s base=%p, using offset fallback", name, (void *)hdr);
      break;
    }
  }
  if (!sym) {
    Log("failed to locate dvacore::string::UTF8to16");
    return;
  }
  if (!LooksLikeFunction(sym)) {
    Log("refusing to hook suspicious address %p", sym);
    return;
  }
  Log("dvacore::string::UTF8to16 at %p", sym);
  void *tramp = BuildTrampoline(sym);
  if (!tramp) {
    Log("failed to allocate trampoline");
    return;
  }
  g_orig = (OrigFn)tramp;
  InstallHook(sym, (void *)&UTF8to16Hook);
  Log("hooked dvacore::string::UTF8to16");
}

__attribute__((constructor)) static void PluginInit() { UTF8FixerInstall(); }

extern "C" int EntryPointFunc(void *, int, void *, void *, void *) { return 0; }
