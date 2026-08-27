// The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT.
// AddressSanitizer's LeakSanitizer fails this program if the Buffer's storage is
// not freed when it goes out of scope.
#include "buffer.hpp"
#include <cassert>

int main()
{
    {
        Buffer b(16);
        for (std::size_t i = 0; i < b.size(); i++) b.at(i) = int(i * i);
        assert(b.at(4) == 16);
        assert(b.size() == 16);
    } // b is destroyed here -- its heap storage must be released
    return 0;
}
