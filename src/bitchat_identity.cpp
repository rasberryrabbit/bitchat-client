/* bitchat_identity.cpp - 키쌍 NVS 영속화 + senderID 파생 */
#include "bitchat_identity.h"
#include "bitchat_packet.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
bool bitchat_identity_init(bitchat_keypair_t* out_kp) {
    if(!out_kp) return false; memset(out_kp,0,sizeof(*out_kp));
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES||err==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();err=nvs_flash_init();}
    if(err!=ESP_OK) return false;
    nvs_handle_t h; if(nvs_open(BCHAT_CLI_NVS_NS,NVS_READWRITE,&h)!=ESP_OK) return false;
    uint8_t seed[32]; size_t sl=sizeof(seed);
    err=nvs_get_blob(h,BCHAT_CLI_NVS_SEED,seed,&sl);
    bool ok=false;
    if(err==ESP_OK&&sl==32) { ok=bitchat_keypair_from_seed(seed,out_kp); }
    else { if(bitchat_generate_keypair(out_kp)){memcpy(seed,out_kp->sk,32);nvs_set_blob(h,BCHAT_CLI_NVS_SEED,seed,32);nvs_commit(h);ok=true;} }
    nvs_close(h); return ok;
}
const uint8_t* bitchat_identity_pk(const bitchat_keypair_t* kp){return kp&&kp->ready?kp->pk:nullptr;}
void bitchat_identity_sender_id(const bitchat_keypair_t* kp, uint8_t out_id[8]) {
    if(!kp||!out_id) return;
    memcpy(out_id, kp->pk, 8); // Ed25519 pk 앞 8바이트를 senderID로 사용
}
