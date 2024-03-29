//
// Created by Ghost on 2019/9/11.
//

#include "drbg_hash.h"

static bool hash_df(DRBG_HASH_CONF *conf,
             const uint8_t *input1, uint32_t input1_length,
             const uint8_t *input2, uint32_t input2_length,
             const uint8_t *input3, uint32_t input3_length,
             uint8_t *output, uint32_t return_length) {

    bool ret = true;
    uint8_t *temp = malloc(conf->out_len);
    uint8_t to_hash[1 + 4];
    uint32_t remain;

    uint32_t input_length = input1_length + input2_length + input3_length;
    // for every round, hash function output outlen bytes data

    // since round is calculated to make output full of data returned
    // we directly calculate remaining bytes to fill up output
    to_hash[0] = 0;
    to_hash[1] = (uint8_t) ((input_length >> 24u) & 0xffu);
    to_hash[2] = (uint8_t) ((input_length >> 16u) & 0xffu);
    to_hash[3] = (uint8_t) ((input_length >> 8u) & 0xffu);
    to_hash[4] = (uint8_t) (input_length & 0xffu);

    for (remain = return_length;; remain -= conf->out_len) {

        to_hash[0]++;

        if ((ret = conf->hash(to_hash, 1 + 4, input1, input1_length,
                input2, input2_length, input3, input3_length, temp)) != true) goto cleanup;

        // use the smaller one size, hash outputs up to hash_size length
        if (remain > conf->out_len)
            memcpy(&output[return_length - remain], temp, conf->out_len);
        else {
            // output remains less than out_len, which reaches final block
            memcpy(&output[return_length - remain], temp, remain);
            break;
        }
    }

    cleanup:
    free(temp);
    return ret;
}

bool DRBG_HASH_new(DRBG_HASH *drbg, DRBG_HASH_CONF *conf) {

    uint8_t *v, *c;

    // validate config
    if (conf == NULL || conf->hash == NULL ||
        conf->seed_len == 0 ||
        conf->out_len == 0 ||
        conf->reseed_interval == 0)
        return false;

    drbg->conf = conf;

    /* initialize internal state */
    v = malloc(conf->seed_len);
    memset(v, 0, conf->seed_len);
    drbg->V = v;
    c = malloc(conf->seed_len);
    memset(c, 0, conf->seed_len);
    drbg->C = c;
    drbg->reseed_counter = 0;
    drbg->prediction_resistance_flag = 0;
    return true;
}

bool DRBG_HASH_instantiate(DRBG_HASH *drbg,
                           const uint8_t *entropy, uint32_t entropy_length,
                           const uint8_t *nonce, uint32_t nonce_length,
                           const uint8_t *pstr, uint32_t pstr_length) {

    uint8_t zero = 0x00;

    // seed_material = entropy_input || nonce || personalization_string
    // V = Hash_df(seed_material, seed_len)
    hash_df(drbg->conf,
            entropy, entropy_length, nonce, nonce_length, pstr, pstr_length,
            drbg->V, drbg->conf->seed_len);

    // C = Hash_df(0x00||V, seed_len)
    hash_df(drbg->conf, &zero, 1, drbg->V, drbg->conf->seed_len,
            NULL, 0,
            drbg->C, drbg->conf->seed_len);

    drbg->reseed_counter = 1;
    return true;

}

bool DRBG_HASH_reseed(DRBG_HASH *drbg,
                      uint8_t *entropy, uint32_t entropy_length,
                      uint8_t *add_input, uint32_t add_length) {

    bool ret;
    uint8_t one = 0x01;
    uint8_t zero = 0x00;
    // seed_material = 0x01 || V || entropy_input || additional_input
    // V = Hash_df(seed_material, seed_len)
    ret = hash_df(drbg->conf,
            &one, 1, entropy, entropy_length, add_input, add_length,
            drbg->V, drbg->conf->seed_len);

    if (ret != true) return ret;

    // C = Hash_df(0x00||V, seed_len)
    ret = hash_df(drbg->conf, &zero, 1, drbg->V, drbg->conf->seed_len,
            NULL, 0,
            drbg->C, drbg->conf->seed_len);

    drbg->reseed_counter = 1;
    return ret;
}

// modify from openssl/crypto/rand/drbg_hash.c
static bool add(uint8_t *dst, uint32_t dst_len,
               const uint8_t *in, uint32_t inlen) {
    uint32_t i;
    uint32_t result;
    const uint8_t *add;
    uint8_t carry = 0, *d;

    if (!(dst_len >= 1 && inlen >= 1 && inlen <= dst_len)) return false;

    d = &dst[dst_len - 1];
    add = &in[inlen - 1];

    for (i = inlen; i > 0; i--, d--, add--) {
        result = *d + *add + carry;
        carry = (uint8_t) (result >> 8u);
        *d = (uint8_t) (result & 0xffu);
    }

    if (carry != 0) {
        /* Add the carry to the top of the dst if inlen is not the same size */
        for (i = dst_len - inlen; i > 0; --i, d--) {
            *d += 1;     /* Carry can only be 1 */
            if (*d != 0) /* exit if carry doesnt propagate to the next byte */
                break;
        }
    }
    return true;
}

static bool hashgen(DRBG_HASH *drbg, uint32_t return_length, uint8_t *output) {

    bool ret;
    // data = V
    uint32_t remain;
    uint8_t one = 1;
    uint8_t *data = malloc(drbg->conf->seed_len);
    memcpy(data, drbg->V, drbg->conf->seed_len);

    // since round is calculated to make output full of data returned
    // we directly calculate remaining bytes to fill up output
    uint8_t *tmp = malloc(drbg->conf->out_len);
    for (remain = return_length;; remain -= drbg->conf->out_len) {

        // W = W || hash(data), till full of return length long bytes
        if ((ret = drbg->conf->hash(data, drbg->conf->seed_len, NULL, 0, NULL, 0, NULL, 0, tmp)) != true) goto cleanup;

        // use the smaller one size, hash outputs up to hash_size length
        if (remain > drbg->conf->out_len) memcpy(&output[return_length - remain], tmp, drbg->conf->out_len);
        else {
            // output remains less than out_len, which reaches final block
            memcpy(&output[return_length - remain], tmp, remain);
            break;
        }

        // data = (data + 1) mod 2^seed_len
        ret = add(data, drbg->conf->seed_len, &one, 1);
        if (ret != true) goto cleanup;
    }

    cleanup:
    free(data);
    free(tmp);
    return ret;
}

bool DRBG_HASH_generate(DRBG_HASH *drbg,
                        uint8_t *add_input, uint32_t add_length,
                        uint8_t *output, uint32_t return_length) {

    bool ret;
    uint8_t *w = malloc(drbg->conf->out_len);
    uint8_t two = 0x02;
    uint8_t three = 0x03;
    uint8_t *htmp = malloc(1 + drbg->conf->seed_len);
    uint8_t *H = malloc(drbg->conf->out_len);
    uint8_t counter[4];
    uint32_t reseed_counter = drbg->reseed_counter;

    if (add_input == NULL) {

        // w = hash(0x02||V||additional_input)
        ret = drbg->conf->hash(&two, 1, drbg->V, drbg->conf->seed_len, add_input, add_length, NULL, 0, w);
        if (ret != true) goto cleanup;

        // V = (V + w) mod 2^seed_len
        ret = add(drbg->V, drbg->conf->seed_len, w, drbg->conf->out_len);
        if (ret != true) goto cleanup;
    }

    ret = hashgen(drbg, return_length, output);
    if (ret != true) goto cleanup;

    // H = hash(0x03||V)
    htmp[0] = 0x03;
    memcpy(&htmp[1], drbg->V, drbg->conf->seed_len);

    if ((ret = drbg->conf->hash(&three, 1, drbg->V, drbg->conf->seed_len, NULL, 0, NULL, 0, H)) != true) goto cleanup;

    // V = (V + H + C + reseed_counter) mod 2^seed_len
    ret = add(drbg->V, drbg->conf->seed_len, H, drbg->conf->out_len);
    if (ret != true) goto cleanup;
    ret = add(drbg->V, drbg->conf->seed_len, drbg->C, drbg->conf->seed_len);
    if (ret != true) goto cleanup;

    counter[0] = (uint8_t) ((reseed_counter >> 24u) & 0xffu);
    counter[1] = (uint8_t) ((reseed_counter >> 16u) & 0xffu);
    counter[2] = (uint8_t) ((reseed_counter >> 8u) & 0xffu);
    counter[3] = (uint8_t) (reseed_counter & 0xffu);
    ret = add(drbg->V, drbg->conf->seed_len, counter, 4);
    drbg->reseed_counter++;

    cleanup:
    free(w);
    free(htmp);
    free(H);
    return ret;
}

bool DRBG_HASH_uninstantiate(DRBG_HASH *drbg) {

    // clear V and C state
    memset(drbg->V, 0, drbg->conf->seed_len);
    free(drbg->V);
    drbg->V = NULL;
    memset(drbg->C, 0, drbg->conf->seed_len);
    free(drbg->C);
    drbg->C = NULL;
    drbg->reseed_counter = 0;

    return true;
}
