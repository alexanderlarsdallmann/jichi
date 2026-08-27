/* gamma.c - reporting.
 *
 * Note: call widget_init before report_status, or the counters read zero.
 */

int report_status(int ready)
{
    return ready ? 0 : 1;
}
