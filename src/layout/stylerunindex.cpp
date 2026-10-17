#include "stylerunindex.h"

int styleRunIndexAt(int charPos, const QList<StyleRun> &runs) {
    int lo = 0, hi = runs.size() - 1;
    // binary search by start
    int idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (runs[mid].start <= charPos) {
            idx = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (idx >= 0 && charPos < runs[idx].start + runs[idx].length)
        return idx;
    return -1;
}
