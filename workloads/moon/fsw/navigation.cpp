#include "navigation.hpp"
#include "constants.hpp"

namespace moon {

NavState navigate(const Sensors& sensors) {
    NavState nav;
    nav.position = sensors.position;
    nav.velocity = sensors.velocity - cross(kMoonSpin, sensors.position);

    const double r = norm(nav.position);
    nav.up = nav.position / r;
    nav.altitude = r - kMoonRadius;
    nav.vertical_speed = dot(nav.velocity, nav.up);
    nav.horizontal_speed = norm(nav.velocity - nav.vertical_speed * nav.up);
    return nav;
}

} // namespace moon
