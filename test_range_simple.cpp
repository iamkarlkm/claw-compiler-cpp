#include <iostream>
#include "runtime/claw_value.h"

using namespace claw::runtime;

int main() {
    // Test make_range
    auto r = ClawValue::make_range(0, 5, 1);
    std::cerr << "is_range: " << r.is_range() << "\n";
    std::cerr << "to_string: " << r.to_string() << "\n";
    
    auto rp = r.as_range_ptr();
    if (rp) {
        std::cerr << "start: " << rp->start << "\n";
        std::cerr << "end: " << rp->end << "\n";
        std::cerr << "step: " << rp->step << "\n";
        std::cerr << "count: " << rp->count() << "\n";
    }
    return 0;
}
