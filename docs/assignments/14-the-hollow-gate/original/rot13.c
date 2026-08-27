/* rot13.c - rotate letters by 13 places; everything else passes through. */

int rot13_char(int c)
{
    if (c >= 'a' && c <= 'z') {
        return 'a' + (c - 'a' + 13) % 26;
    }
    if (c >= 'A' && c <= 'Z') {
        return 'a' + (c - 'A' + 13) % 26;
    }
    return c;
}
