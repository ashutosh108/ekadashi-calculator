#include "catch.hpp"

#include <sstream>

#include "date.h"

TEST_CASE("Date keeps year, month and day") {
    Date d{2019, 3, 19};
    REQUIRE(d.year == 2019);
    REQUIRE(d.month == 3);
    REQUIRE(d.day == 19);
}

TEST_CASE("Printing date") {
    std::stringstream s;
    s << Date{2019, 3, 19};
    REQUIRE(s.str() == "2019-03-19");
}

TEST_CASE("Can compare Date for equality") {
    Date d1{2019, 3, 19};
    Date d2{2019, 3, 19};
    Date d3{2019, 3, 20};
    REQUIRE(d1 == d2);
    REQUIRE(d1 != d3);
}
