/* SPDX-License-Identifier: MIT */
/* Supplied freestanding PE child: observe the actual Windows environment. */
typedef unsigned short U16;
typedef unsigned long long H;
typedef unsigned int U32;
#define API(ret,name,args) extern ret (__attribute__((ms_abi)) *name) args
API(U16 *,GetCommandLineW,(void));
API(U16 **,CommandLineToArgvW,(const U16 *,int *));
API(U16 *,GetEnvironmentStringsW,(void));
API(int,FreeEnvironmentStringsW,(U16 *));
API(U32,GetEnvironmentVariableW,(const U16 *,U16 *,U32));
API(U32,GetCurrentDirectoryW,(U32,U16 *));
API(H,GetStdHandle,(U32));
API(int,ReadFile,(H,void *,U32,U32 *,void *));
API(int,WriteFile,(H,const void *,U32,U32 *,void *));
API(void,ExitProcess,(U32));
struct FileInfo { U32 attributes,times[6],volume,size_high,size_low,links,index_high,index_low; };
API(int,GetFileInformationByHandle,(H,struct FileInfo *));
static int equal(const U16 *a,const U16 *b) { while (*a && *a==*b) { ++a;++b; } return *a==*b; }
void entry(void) {
  int argc=0,count=0;
  U16 **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  if (!argv || argc<2) ExitProcess(98);
  U32 amount=0; unsigned char byte;
  if (equal(argv[1],u"--inherit")) {
    H identities[4]={0}; int field=0;
    for (U16 *p=argv[2];*p && field<4;++p) if (*p==':') ++field; else if (*p>='0' && *p<='9') identities[field]=identities[field]*10+*p-'0';
    struct FileInfo info;
    if (GetFileInformationByHandle(identities[0],&info) && info.volume==identities[1] && info.index_high==identities[2] && info.index_low==identities[3]) ExitProcess(98);
    unsigned char data[16],expected[]={'I',0,255,'N','\n'},errors[]={'E',0,254,'R'}; U32 n=0,written=0;
    int pass=ReadFile(GetStdHandle(-10u),data,16,&n,0) && n==5;
    for (int i=0;i<5;++i) pass=pass && data[i]==expected[i];
    pass=pass && !(ReadFile(GetStdHandle(-10u),data,16,&n,0) && n);
    pass=pass && WriteFile(GetStdHandle(-11u),expected,5,&written,0) && written==5;
    pass=pass && WriteFile(GetStdHandle(-12u),errors,4,&written,0) && written==4;
    ExitProcess(pass ? 37 : 98);
  }
  if (equal(argv[1],u"--hang")) {
    WriteFile(GetStdHandle(-11u),"R",1,&amount,0);
    while (ReadFile(GetStdHandle(-10u),&byte,1,&amount,0) && amount) {}
    ExitProcess(37);
  }
  U16 *environment=GetEnvironmentStringsW();
  if (environment) for (U16 *p=environment;*p;++count) { while (*p) ++p; ++p; }
  if (equal(argv[1],u"--empty")) ExitProcess(count ? 98 : 37);
  U16 value[4096],cwd[4096],wanted[4096];
  if (equal(argv[1],u"--snapshot")) {
    U32 n=GetEnvironmentVariableW(u"QAPLET_SNAPSHOT",value,4096);
    int pass=n && equal(value,u"frozen") && !GetEnvironmentVariableW(u"QAPLET_AFTER_CREATE",value,4096);
    ExitProcess(pass ? 37 : 98);
  }
  int args=argc==9 && equal(argv[2],u"") && equal(argv[3],u"参数 空格") &&
      equal(argv[4],u"two words") && equal(argv[5],u"quote\"double") &&
      equal(argv[6],u"slash\\tail\\") && equal(argv[7],u"literal&|;$()<>*") && equal(argv[8],u"apostrophe'");
  U32 n=GetEnvironmentVariableW(u"QAPLET_ONLY",value,4096);
  int env=count==2 && n && equal(value,u"完整 值");
  U32 have=GetCurrentDirectoryW(4096,cwd),need=GetEnvironmentVariableW(u"QAPLET_CWD",wanted,4096);
  int eof=!(ReadFile(GetStdHandle(-10u),&byte,1,&amount,0) && amount);
  if (environment) FreeEnvironmentStringsW(environment);
  ExitProcess(args && env && have && need && equal(cwd,wanted) && eof ? 37 : 98);
}
