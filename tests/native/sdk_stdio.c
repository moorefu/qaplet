/* SPDX-License-Identifier: MIT */
/* Public SDK and real Host standard stream boundary. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <stdio.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"stdio contract line %d: %s\n",__LINE__,#c);return 1;} } while(0)
int sdk_stdio(int argc,char **argv) {
  (void)argc;(void)argv;
  qaplet_abi abi=QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK);
  CHECK(abi.capabilities & (1u<<24));
  puts("M2 stdio capability: explicit opt-in available: pass");return 0;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv){return sdk_stdio(argc,argv);}
#endif
