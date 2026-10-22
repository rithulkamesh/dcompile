// protection.h: undoing the executable's code scrambling.
//
//
// The Mac build of the game stores __TEXT,__text XOR-scrambled with a repeating four-byte key (the phase is the file offset). A
// small window around the entry point, the C runtime `start` stub, is left in the clear because it runs before anything could
// unscramble the rest. The key is not hard-coded here: it is recovered from the section itself.
//
// x86 code is dominated by a few byte values (0x00 above all), so for each of the four phases the most frequent bytes are
// candidates for the key byte. Every combination of the top candidates is scored by how many function prologues
// (`push ebp; mov ebp, esp`) and epilogues (`leave; ret`) it produces; the best-scoring key wins. A key of zero means the
// section was not scrambled.

#define PLAIN_BEFORE_ENTRY 2   // also covers the `leave; ret` that ends the function just before `start`
#define PLAIN_AFTER_ENTRY 0x20 // the 32-byte start stub

static int score_key(const u8 *sec, u32 n, u32 file_off, const u8 key[4]) {
    int score = 0;
    for (u32 i = 0; i + 3 < n; i++) {
        u8 a = sec[i] ^ key[(file_off + i) & 3], b = sec[i + 1] ^ key[(file_off + i + 1) & 3];
        u8 c2 = sec[i + 2] ^ key[(file_off + i + 2) & 3];
        if (a == 0x55 && b == 0x89 && c2 == 0xe5)
            score += 2; // push ebp; mov ebp, esp
        else if (a == 0xc9 && b == 0xc3)
            score += 1; // leave; ret
    }
    return score;
}

// Recovers the key of a scrambled section. `sec` points at the first `n` bytes of the section as stored in the file.
// Returns 1 and fills `key` if the section is scrambled, 0 if it is plain (or the data does not look like x86 code at all).
static int recover_text_key(const u8 *sec, u32 n, u32 file_off, u8 key[4]) {
    u32 sample = n < (256u << 10) ? n : (256u << 10);
    u8 cand[4][4];
    for (int ph = 0; ph < 4; ph++) {
        u32 hist[256] = {0};
        for (u32 i = 0; i < sample; i++)
            if (((file_off + i) & 3) == (u32)ph)
                hist[sec[i]]++;
        for (int k = 0; k < 4; k++) { // the four most frequent byte values of this phase
            int best = 0;
            for (int v = 1; v < 256; v++)
                if (hist[v] > hist[best])
                    best = v;
            cand[ph][k] = (u8)best;
            hist[best] = 0;
        }
    }
    int best_score = -1, plain_score = score_key(sec, sample, file_off, (const u8[4]){0, 0, 0, 0});
    for (int combo = 0; combo < 256; combo++) {
        u8 k[4] = {cand[0][combo & 3], cand[1][(combo >> 2) & 3], cand[2][(combo >> 4) & 3],
                   cand[3][(combo >> 6) & 3]};
        int s = score_key(sec, sample, file_off, k);
        if (s > best_score) {
            best_score = s;
            memcpy(key, k, 4);
        }
    }
    return best_score > 4 * plain_score + 64;
}

// Unscrambles guest memory in place. `addr` is the section's guest address, `file_off` its file offset; `entry` is the program
// entry point (whose start stub is left alone).
static void unscramble_text(u32 addr, u32 size, u32 file_off, const u8 *as_stored, u32 entry) {
    u8 key[4];
    if (!recover_text_key(as_stored, size, file_off, key))
        return;
    printf("__text is scrambled; recovered key %02x %02x %02x %02x\n", key[0], key[1], key[2], key[3]);
    for (u32 o = 0; o < size; o++) {
        u32 ad = addr + o;
        if (ad < entry - PLAIN_BEFORE_ENTRY || ad >= entry + PLAIN_AFTER_ENTRY)
            G[ad] ^= key[(file_off + o) & 3];
    }
}
