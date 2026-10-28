/* bitchat_crypto.cpp - Ed25519 (libsodium) */
#include "bitchat_crypto.h"
#include <sodium.h>
#include <string.h>
static bool g_init=false;
void bitchat_crypto_init(void){if(g_init)return;sodium_init();g_init=true;}
bool bitchat_generate_keypair(bitchat_keypair_t* kp){if(!g_init)bitchat_crypto_init();if(!kp)return false;memset(kp,0,sizeof(*kp));if(crypto_sign_keypair(kp->pk,kp->sk)!=0)return false;kp->ready=true;return true;}
bool bitchat_keypair_from_seed(const uint8_t seed[32],bitchat_keypair_t* kp){if(!g_init)bitchat_crypto_init();if(!seed||!kp)return false;memset(kp,0,sizeof(*kp));if(crypto_sign_seed_keypair(kp->pk,kp->sk,seed)!=0)return false;kp->ready=true;return true;}
bool bitchat_sign(const bitchat_keypair_t* kp,const uint8_t* msg,size_t len,uint8_t* sig,size_t sig_max){if(!g_init)bitchat_crypto_init();if(!kp||!msg||!sig||!kp->ready||sig_max<64)return false;unsigned long long sl=0;if(crypto_sign_detached(sig,&sl,msg,(unsigned long long)len,kp->sk)!=0)return false;return sl==64;}
bool bitchat_verify(const uint8_t* pk,size_t pk_len,const uint8_t* msg,size_t len,const uint8_t* sig,size_t sig_len){if(!g_init)bitchat_crypto_init();if(!pk||!msg||!sig)return false;if(pk_len!=32||sig_len!=64)return false;return crypto_sign_verify_detached(sig,msg,(unsigned long long)len,pk)==0;}
