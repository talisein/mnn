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

#pragma once

#include "peeler/props.hpp"
#include <peel/RefPtr.h>
#include <peel/Shumate/Coordinate.h>
#include <peel/enum.h>

namespace mnn
{
    /* How exactly a position marks the station. Explicit values: stored in
     * CallsignPosition's precision column. */
    enum class Precision : int
    {
        precise = 0,     /* callook's coordinates, or a street-address match */
        approximate = 1, /* a ZIP code centroid */
    };
}
PEEL_ENUM(mnn::Precision)

namespace mnn
{
    /* A station's position, as PositionLookup::lookup() yields it. */
    struct Position
    {
        peel::RefPtr<peel::Shumate::Coordinate> coordinate;
        Precision                               precision = Precision::precise;
    };
}

PEELER_BOXED_TYPE(mnn::Position, "mnn-Position")
