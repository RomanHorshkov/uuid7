#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <uuid7.h>

int main(void)
{
    assert(uuid7_init(NULL, NULL) == 0);

    unsigned char a[16];
    unsigned char b[16];
    assert(uuid7_gen(a) == 0);
    assert(uuid7_gen(b) == 0);
    assert(memcmp(a, b, sizeof a) != 0); /* two generated ids must differ */
    assert(memcmp(a, b, sizeof a) < 0);  /* monotonic: a was generated first, sorts first */

    printf("smoke test: installed package generates monotonic ids correctly\n");
    return 0;
}
