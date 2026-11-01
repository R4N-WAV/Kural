// Kural DSP tests (no framework). Exits non-zero if any check fails. Run in CI on every push.
#include "TestSupport.h"

void pitchTests();
void harmonyTests();
void shifterTests();
void correctionTests();
void fxTests();

int main()
{
    std::printf ("---- pitch detection\n");
    pitchTests();
    std::printf ("\n---- harmony planning\n");
    harmonyTests();
    std::printf ("\n---- pitch shifting\n");
    shifterTests();
    std::printf ("\n---- pitch correction\n");
    correctionTests();
    std::printf ("\n---- effects\n");
    fxTests();
    using namespace kural::test;
    std::printf ("\n%d of %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
