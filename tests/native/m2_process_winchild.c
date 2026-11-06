/* SPDX-License-Identifier: MIT */
/* Freestanding Windows child: no libc startup or implicit environment fields. */
typedef unsigned short U16;
typedef unsigned long long H;
typedef unsigned int U32;
#define API(ret, name, args) extern ret (__attribute__((ms_abi)) *name) args
API(U16 *, GetCommandLineW, (void));
API(U16 **, CommandLineToArgvW, (const U16 *, int *));
API(U16 *, GetEnvironmentStringsW, (void));
API(int, FreeEnvironmentStringsW, (U16 *));
API(U32, GetEnvironmentVariableW, (const U16 *, U16 *, U32));
API(U32, GetCurrentDirectoryW, (U32, U16 *));
API(H, GetStdHandle, (U32));
API(int, ReadFile, (H, void *, U32, U32 *, void *));
API(int, WriteFile, (H, const void *, U32, U32 *, void *));
API(int, GetHandleInformation, (H, U32 *));
struct FileInfo { U32 attributes, times[6], volume, size_high, size_low, links, index_high, index_low; };
API(int, GetFileInformationByHandle, (H, struct FileInfo *));
API(H, LocalFree, (H));
API(void, ExitProcess, (U32));
API(void, Sleep, (U32));
static int equal(const U16 *a, const U16 *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
void entry(void) {
  int argc = 0, count = 0;
  U16 **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc == 2 && equal(argv[1], u"--ready-hang")) {
    U32 written = 0;
    WriteFile(GetStdHandle(-11u), "R", 1, &written, 0);
    for (;;) Sleep(-1u);
  }
  U16 *environment = GetEnvironmentStringsW();
  if (environment) for (U16 *p = environment; *p; ++count) { while (*p) ++p; ++p; }
  U16 value[4096], cwd[4096], wanted[4096];
  U32 length = GetEnvironmentVariableW(u"QAPLET_ONLY", value, 4096);
  U32 cwdlength = GetCurrentDirectoryW(4096, cwd);
  U32 wantedlength = GetEnvironmentVariableW(u"QAPLET_CWD", wanted, 4096);
  int env = count == 2 && length == 4 && equal(value, u"完整 值") &&
            !GetEnvironmentVariableW(u"QAPLET_PARENT_LEAK", value, 4096);
  int args = argc == 10 && equal(argv[1], u"--child") && equal(argv[2], u"") &&
      equal(argv[3], u"参数 空格") && equal(argv[4], u"two words") && equal(argv[5], u"quote\"double") &&
      equal(argv[6], u"slash\\tail\\") && equal(argv[7], u"literal&|;$()<>*") && equal(argv[8], u"apostrophe'");
  H identities[4] = {0};
  if (argc == 10) {
    int field = 0;
    for (U16 *p = argv[9]; *p && field < 4; ++p)
      if (*p == ':') ++field;
      else if (*p >= '0' && *p <= '9') identities[field] = identities[field] * 10 + *p - '0';
  }
  H sentinel = identities[0];
  U32 flags = 0;
  struct FileInfo info;
  int valid = sentinel && GetHandleInformation(sentinel, &flags);
  int leaked = valid && GetFileInformationByHandle(sentinel, &info) && info.volume == identities[1] &&
      info.index_high == identities[2] && info.index_low == identities[3];
  if (argc == 10 && equal(argv[1], u"--sentinel-check")) ExitProcess(leaked ? 44 : 98);
  unsigned char diagnostic[6] = {args, env, cwdlength && wantedlength && equal(cwd, wanted), count, !leaked, valid && !leaked};
  unsigned char data[16], errorbytes[] = {'E', 0, 254, 'R'};
  U32 total = 0, amount = 0, written = 0;
  while (total < 16 && ReadFile(GetStdHandle(-10u), data + total, 16 - total, &amount, 0) && amount) total += amount;
  int exact = total == 5 && data[0] == 'I' && data[1] == 0 && data[2] == 255 && data[3] == 'N' && data[4] == '\n';
  int out = WriteFile(GetStdHandle(-11u), data, total, &written, 0) && written == total;
  int err = WriteFile(GetStdHandle(-12u), diagnostic, 6, &written, 0) && written == 6;
  err &= WriteFile(GetStdHandle(-12u), errorbytes, 4, &written, 0) && written == 4;
  if (environment) FreeEnvironmentStringsW(environment);
  if (argv) LocalFree((H)argv);
  ExitProcess(args && env && diagnostic[2] && !leaked && exact && out && err ? 37 : 98);
}
