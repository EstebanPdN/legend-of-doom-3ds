// Host I/O harness: real transfer/hash/control flow, fake console install service.
#include <assert.h>
#include <stdarg.h>
#include "updater.c"
static bool close_request,wrong_title,short_write,no_space;
static unsigned starts,finishes,cancels,written;
static FILE *input_file;
static Result ac_result,soc_result,ssl_result;
static unsigned ac_closed,soc_closed,ssl_closed;
static int created_priority;
static s32 caller_priority=0x30;
Result acInit(void){return ac_result;}Result ACU_GetWifiStatus(u32*w){*w=1;return 0;}void acExit(void){ac_closed++;}
void *memalign(size_t align,size_t n){return malloc(n);}
Result socInit(void*p,u32 n){assert(n==1024*1024);return soc_result;}void socExit(void){soc_closed++;}
Result sslcInit(Handle h){assert(h==0);return ssl_result;}void sslcExit(void){ssl_closed++;}
Result svcGetThreadPriority(s32*p,Handle h){*p=caller_priority;return 0;}
Thread threadCreate(void(*f)(void*),void*a,unsigned stack,int priority,int cpu,bool detached){created_priority=priority;return (Thread)1;}
Result threadJoin(Thread t,u64 timeout){return 0;}void threadFree(Thread t){}bool envIsHomebrew(void){return false;}
bool aptShouldClose(void){return close_request;}bool aptIsActive(void){return true;}
bool aptIsHomeAllowed(void){return true;}bool aptIsSleepAllowed(void){return true;}
void aptSetHomeAllowed(bool b){}void aptSetSleepAllowed(bool b){}
void Platform3DS_Debug(const char*f){(void)f;}
Result FSUSER_GetSdmcArchiveResource(FS_ArchiveResource*r){r->freeClusters=no_space?0:100000;r->clusterSize=4096;return 0;}
Result amInit(void){return 0;}void amExit(void){}
Result FSUSER_OpenFileDirectly(Handle*h,int a,FS_Path b,FS_Path c,int d,int e){input_file=fopen(UPDATE_PART,"rb");*h=1;return input_file?0:-1;}
Result AM_GetCiaFileInfo(int a,AM_TitleInfo*i,Handle h){i->titleID=wrong_title?123:0x0004000005a2d000ull;return 0;}
Result AM_GetCiaRequiredSpace(u64*r,int a,Handle h){*r=5000000;return 0;}
Result AM_StartCiaInstallOverwrite(Handle*h,int a){starts++;*h=2;return 0;}
Result FSFILE_Read(Handle h,u32*n,u64 off,void*b,u32 s){fseek(input_file,off,SEEK_SET);*n=fread(b,1,s,input_file);return 0;}
Result FSFILE_Write(Handle h,u32*n,u64 off,const void*b,u32 s,u32 f){*n=short_write?s-1:s;written+=*n;return 0;}
Result AM_FinishCiaInstall(Handle h){finishes++;return 0;}
Result AM_CancelCIAInstall(Handle h){cancels++;return 0;}
Result FSFILE_Close(Handle h){fclose(input_file);return 0;}
int main(int argc,char**argv){
 initialized=true; status.state=UPDATE_CHECKING;
 // Exercise the actual worker's partial initialization and cleanup paths.
 ac_result=-1;run_job(NULL);assert(status.state==UPDATE_ERROR&&!ac_closed&&!soc_closed&&!ssl_closed);
 ac_result=0;soc_result=-2;run_job(NULL);assert(!strcmp(status.message,"SOCKET SERVICE FAILED")&&ac_closed==1&&!soc_closed&&!ssl_closed);
 soc_result=0;ssl_result=-3;run_job(NULL);assert(!strcmp(status.message,"TLS SERVICE FAILED")&&ac_closed==2&&soc_closed==1&&!ssl_closed);
 ssl_result=0;
 // HTTP/TLS must not be scheduled behind the always-runnable 3D renderer.
 start(false);assert(created_priority==0x2f&&busy);busy=false;
 caller_priority=0x18;start(false);assert(created_priority==0x18);busy=false;
 // Exercise the real bounded transfer and hash paths with deterministic bytes.
 unsigned char payload[8192];for(unsigned i=0;i<sizeof(payload);i++)payload[i]=i*13;
 release.size=sizeof(payload);
 mbedtls_sha256_context sha;unsigned char digest[32];
 mbedtls_sha256_init(&sha);mbedtls_sha256_starts_ret(&sha,0);
 mbedtls_sha256_update_ret(&sha,payload,sizeof(payload));mbedtls_sha256_finish_ret(&sha,digest);
 mbedtls_sha256_free(&sha);
 for(int i=0;i<32;i++)snprintf(release.sha256+i*2,3,"%02x",digest[i]);
 Transfer t={.file=fopen(UPDATE_PART,"wb"),.expected=release.size};assert(t.file);
 assert(receive(payload,1,sizeof(payload),&t)==sizeof(payload));assert(!fclose(t.file));
 assert(t.size==release.size && verify_file());
 // A wrong app or insufficient space must never start an install transaction.
 wrong_title=true;assert(!install_cia()&&starts==0);wrong_title=false;
 no_space=true;assert(!install_cia()&&starts==0);no_space=false;
 short_write=true;assert(!install_cia()&&starts==1&&cancels==1&&!finishes);short_write=false;
 written=0;assert(install_cia()&&finishes==1&&written==release.size);
 cancel=true;assert(!verify_file());assert(!install_cia());cancel=false;
 FILE*f=fopen(UPDATE_PART,"r+b");assert(f);fputc(42,f);fclose(f);assert(!verify_file());
 f=fopen(UPDATE_PART,"wb");assert(f);fwrite("3DSXtest",1,8,f);fclose(f);assert(!verify_file());
 Transfer limited={.expected=4,.file=tmpfile()};assert(!receive("12345",1,5,&limited));fclose(limited.file);
 limited=(Transfer){.size=5,.expected=4,.file=tmpfile()};assert(!receive("x",1,1,&limited));fclose(limited.file);
 Transfer memory={0};cancel=true;assert(!receive("123",1,3,&memory));cancel=false;
 strcpy(launch_file,"sdmc:/test.3dsx");f=fopen(launch_file,"wb");fwrite("OLD",1,3,f);fclose(f);
 assert(install_3dsx());f=fopen(launch_file,"rb");char b[8];assert(fread(b,1,8,f)==8&&!memcmp(b,"3DSXtest",8));fclose(f);
 if(argc>1 && !strcmp(argv[1],"--live-check")) {
   assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
   Transfer live={0};assert(fetch("https://api.github.com/repos/" UPDATE_REPOSITORY "/releases?per_page=100",&live));
   UpdateRelease found;
   assert(Update_ParseRelease(live.data,live.size,false,false,&found)>=0);
   assert(Update_ParseRelease(live.data,live.size,true,false,&found)>=0);
   free(live.data);curl_global_cleanup();
   status.prerelease=false;download_job=false;
   run_job(NULL);assert(status.state==UPDATE_CURRENT||status.state==UPDATE_AVAILABLE||status.state==UPDATE_EMPTY);
   assert(ssl_closed==1&&soc_closed==2&&ac_closed==3&&!busy);
   puts("PASS: live GitHub HTTPS with bundled CA, and both release channels");
 }
 puts("PASS: bounded transfer/SHA256; corruption/truncation/cancel/size; title/space/short-write/commit guards. AM is mocked, not console-tested.");
}
