#include "navigation.hpp"
#include "constants.hpp"
#include "moon_ephemeris.hpp"

namespace moon {

NavState navigate(const Sensors& sensors) {
    const Vec3 moon_r = moon_position(sensors.t);
    const Vec3 moon_v = moon_velocity(sensors.t);

    NavState nav;
    nav.position = sensors.position - moon_r;
    nav.velocity = (sensors.velocity - moon_v) - cross(moon_spin(moon_r, moon_v), nav.position);

    const double r = norm(nav.position);
    nav.up = nav.position / r;
    nav.altitude = r - kMoonRadius;
    nav.vertical_speed = dot(nav.velocity, nav.up);
    nav.horizontal_speed = norm(nav.velocity - nav.vertical_speed * nav.up);
    return nav;
}

} // namespace moon
