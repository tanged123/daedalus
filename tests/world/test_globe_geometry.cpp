#include "daedalus/world/globe_renderer.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

TEST(GlobeGeometry, GraticuleSegmentCountsFor15DegStep) {
    const auto geometry = daedalus::world::GlobeRenderer::build_graticule(15);

    EXPECT_EQ(geometry.dim_segment_count, 4320u);
    EXPECT_EQ(geometry.major_segment_count, 3960u);
    EXPECT_EQ(geometry.base_segment_count(), 8280u);

    EXPECT_EQ(geometry.dim_vertices.size(), geometry.dim_segment_count * 2);
    EXPECT_EQ(geometry.major_vertices.size(), geometry.major_segment_count * 2);
}

TEST(GlobeGeometry, SpecialLineSetIsPresent) {
    const auto geometry = daedalus::world::GlobeRenderer::build_graticule(15);

    EXPECT_EQ(geometry.special_line_count, 6u);
    EXPECT_EQ(geometry.special_segment_count, 1980u);
    EXPECT_EQ(geometry.special_vertices.size(), geometry.special_segment_count * 2);
}

TEST(GlobeGeometry, RejectsInvalidStep) {
    EXPECT_THROW((void)daedalus::world::GlobeRenderer::build_graticule(0), std::invalid_argument);
}

} // namespace
