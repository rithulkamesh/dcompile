// build_id.h: recognising the supported game build.
//
// The binary patches (patches.h) contain addresses that only make sense for one exact executable, so the build is identified by
// the SHA-256 of the main executable. Unknown builds still load; they just run without the patches.

#include <CommonCrypto/CommonDigest.h>

typedef struct {
    const char *name, *sha256;
} KnownBuild;
static const KnownBuild known_builds[] = {
    {"Assassin's Creed II, Mac, Steam", "8ea95067058dc857c711bc46a8b42ee0f84196a361cae24ad3c8832d1487fae5"},
};

static void sha256_hex(const u8 *data, size_t n, char out[65]) {
    u8 d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, (CC_LONG)n, d);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
        snprintf(out + 2 * i, 3, "%02x", d[i]);
}

static const char *identify_build(const u8 *file, size_t n) {
    char hex[65];
    sha256_hex(file, n, hex);
    for (size_t i = 0; i < sizeof known_builds / sizeof *known_builds; i++)
        if (!strcmp(hex, known_builds[i].sha256))
            return known_builds[i].name;
    return NULL;
}
