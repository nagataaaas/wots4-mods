// Mod entry point. Called once by the ModLoader. Return TRUE to accept, FALSE to refuse.
#include <wots4/mod_api.h>

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if(!context||context->size<sizeof(*context)||context->version!=wots4::modApiVersion||!context->host)
        return FALSE;
    if(context->host->log)
        context->host->log(context->module,"example mod initialized");
    return TRUE;
}
