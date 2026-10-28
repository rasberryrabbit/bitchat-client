/*
 * bitchat_identity.cpp - 자체 키쌍 NVS 영속화
 */

#include "bitchat_identity.h"
#include "bitchat_packet.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

bool bitchat_identity_init(bitchat_keypair_t* out_kp) {
    if (!out_kp) return false;
    memset(out_kp, 0, sizeof(*out_kp));

    // NVS 초기화
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return false;

    nvs_handle_t h;
    if (nvs_open(BCHAT_CLI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    uint8_t seed[32];
    size_t seed_len = sizeof(seed);
    err = nvs_get_blob(h, BCHAT_CLI_NVS_SEED, seed, &seed_len);

    bool ok = false;
    if (err == ESP_OK && seed_len == 32) {
        // seed에서 결정론적 키쌍 복원
        ok = bitchat_keypair_from_seed(seed, out_kp);
    } else {
        // 새로운 seed 생성 + 저장
        if (bitchat_generate_keypair(out_kp)) {
            // libsodium sk의 앞 32바이트가 seed
            memcpy(seed, out_kp->private_key, 32);
            nvs_set_blob(h, BCHAT_CLI_NVS_SEED, seed, sizeof(seed));
            nvs_commit(h);
            ok = true;
        }
    }
    nvs_close(h);
    return ok;
}

const uint8_t* bitchat_identity_pk(const bitchat_keypair_t* kp) {
    if (!kp || !kp->ready) return nullptr;
    return kp->public_key;
}

void bitchat_identity_node_id(const bitchat_keypair_t* kp, char out_id[13]) {
    if (!kp || !out_id) return;
    bitchat_id_from_pk(kp->public_key, out_id);
}
