#include "hyperion.hpp"
#include <iostream>
#include <cassert>

int main() {
    ArenaError ae;
    // Initialize 64MB Arena with 1024 Slots.
    auto db = Hyperion::create(64 * 1024 * 1024, 1024, ae);
    if (ae != ArenaError::None) {
        std::cerr << "Fatal: Hyperion initialization failed.\n"; 
        return 1;
    }

    // 1. Basic Put/Get Verification
    assert(db.put("user:1001", "balance:5000") == Status::OK);
    std::string val;
    assert(db.get("user:1001", val) == Status::OK);
    assert(val == "balance:5000");

    // 2. Overwrite / Mutability Verification
    assert(db.put("user:1001", "balance:4500") == Status::OK);
    assert(db.get("user:1001", val) == Status::OK);
    assert(val == "balance:4500");

    // 3. Deletion & Tombstone Verification
    assert(db.del("user:1001") == Status::OK);
    assert(db.get("user:1001", val) == Status::NotFound);

    // 4. Tombstone Recycling
    // Insert new key colliding with old slot, ensuring reuse logic works.
    assert(db.put("user:1001", "balance:0") == Status::OK);
    assert(db.get("user:1001", val) == Status::OK);
    assert(val == "balance:0");

    std::cout << "Hyperion Integrity Check: PASSED.\n";
    return 0;
}