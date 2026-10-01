#include "crash_note.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_private/panic_internal.h"
#include "xtensa_context.h"
#include <cstdint>
// Capture only fault metadata in retained RAM; no flash write, allocation, locks,
// microphone data or application payloads in the panic path. SDK still owns reset.
struct CrashNote {uint32_t magic,pc,cause,address,core,return_address,stack;char reason[192];};
RTC_NOINIT_ATTR static volatile CrashNote note;
extern "C" bool g_panic_abort;
extern "C" char* g_panic_abort_details;
extern "C" void __real_esp_panic_handler(panic_info_t*);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info){
 if(!info || !info->frame){__real_esp_panic_handler(info);return;}
 note.magic=0;
 const XtExcFrame* frame=static_cast<const XtExcFrame*>(info->frame);
 note.pc=frame->pc;note.cause=frame->exccause;note.address=frame->excvaddr;note.core=info->core;note.return_address=frame->a0;note.stack=frame->a1;
 const char* reason=g_panic_abort?g_panic_abort_details:info->reason;
 unsigned i=0;if(reason)for(;i<sizeof(note.reason)-1 && reason[i];i++)note.reason[i]=reason[i];note.reason[i]=0;
 note.magic=0xd1770bad;
 __real_esp_panic_handler(info);
}
void crash_note_status(cJSON* out){
 if(note.magic!=0xd1770bad)return;
 cJSON* c=cJSON_AddObjectToObject(out,"last_crash");
 cJSON_AddNumberToObject(c,"pc",note.pc);cJSON_AddNumberToObject(c,"cause",note.cause);cJSON_AddNumberToObject(c,"address",note.address);
 cJSON_AddNumberToObject(c,"core",note.core);cJSON_AddNumberToObject(c,"return_address",note.return_address);cJSON_AddNumberToObject(c,"stack",note.stack);
 char text[192];for(unsigned i=0;i<sizeof(text);i++)text[i]=note.reason[i];text[191]=0;cJSON_AddStringToObject(c,"reason",text);
}
