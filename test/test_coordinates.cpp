/*
  monday-night-net: An amateur radio net monitoring utility in gtk4
  Copyright (C) 2026  Andrew Potter

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <boost/ut.hpp>
#include <cmath>
#include "coordinates.hpp"

int main() {
    using namespace boost::ut;

    "distance_miles"_test = [] {
        expect(eq(0.0, mnn::distance_miles(37.3894, -122.0819, 37.3894, -122.0819)));

        // One degree of latitude along a meridian.
        expect(std::abs(mnn::distance_miles(37.0, -122.0, 38.0, -122.0) - 69.09) < 0.05);

        // Mountain View city hall to Palo Alto city hall, about 6 miles.
        auto mv_pa = mnn::distance_miles(37.3894, -122.0819, 37.4443, -122.1598);
        expect(mv_pa > 5.5 && mv_pa < 6.5) << mv_pa;

        // Symmetric.
        expect(eq(mv_pa, mnn::distance_miles(37.4443, -122.1598, 37.3894, -122.0819)));
    };
}
