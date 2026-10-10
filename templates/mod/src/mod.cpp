// Mod の入口。ModLoader から一度だけ呼ばれる。TRUE を返すと受け入れ、FALSE を返すと断る。
#include <wots4/mod_api.h>

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if(!context||context->size<sizeof(*context)||context->version!=wots4::modApiVersion||!context->host)
        return FALSE;
    if(context->host->log)
        context->host->log(context->module,"example mod initialized");
    return TRUE;
}
