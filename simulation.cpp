//
// Created by Omar Richardson on 24/10/2018.
// Hints for optimization:
// 2. Be careful with compute_next_impact() calls
// 3. Just store the number of particles in the left ball
//

#include "simulation.h"

const double PI = 3.14159265358979323;
const double EPS = 1E-14;
#define px x_pos[particle]
#define py y_pos[particle]

template<typename T>
int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

Simulation::Simulation(const int &num_particles, const double &bridge_height, const double &circle_radius,
                       const double &circle_distance, const int &left_gate_capacity,
                       const int &right_gate_capacity, const bool &random_dir, const bool &flat_gate)
        : num_particles(num_particles), circle_radius(circle_radius),
          circle_distance(circle_distance), bridge_height(bridge_height),
          left_gate_capacity(left_gate_capacity), right_gate_capacity(right_gate_capacity),
          explosion_direction_is_random(random_dir), gate_is_flat(flat_gate) {
    rd = std::make_shared<std::random_device>();
    rng = std::make_shared<std::mt19937>((*rd)());
    unif_real = std::make_shared<std::uniform_real_distribution<double>>(0, 1);
    bridge_length = 0;
}

void Simulation::setup() {
    next_impact_times.resize(num_particles);
    sorted_indices.resize(num_particles);
    impact_times.resize(num_particles);
    in_left_gate.resize(num_particles);
    in_right_gate.resize(num_particles);
    x_pos.resize(num_particles);
    y_pos.resize(num_particles);
    next_x_pos.resize(num_particles);
    next_y_pos.resize(num_particles);
    directions.resize(num_particles);
    next_directions.resize(num_particles);
    gate_arrays.push_back(in_left_gate);
    gate_arrays.push_back(in_right_gate);
    gate_contents.push_back(currently_in_left_gate);
    gate_contents.push_back(currently_in_right_gate);
    gate_capacities.push_back(left_gate_capacity);
    gate_capacities.push_back(right_gate_capacity);
    couple_bridge();
    left_center_x = -circle_distance / 2 - circle_radius;
    right_center_x = circle_distance / 2 + circle_radius;
    max_path = circle_distance + bridge_height + circle_radius * 4; // Upper bound for the longest path
    if (debug) {
        std::string debug_file_name = "debug_logging/" + get_random_string(7) + ".debug";
        std::cout << "Storing debugging information in " + debug_file_name << std::endl;
        debug_file.open(debug_file_name, std::ofstream::out);
        debug_file << "num_particles\tcircle_radius\tcircle_distance\tbridge_height\tbridge_length\tthreshold\n";
        debug_file << num_particles << "\t" << circle_radius << "\t" << circle_distance << "\t"
                   << bridge_height << "\t" << bridge_length << "\t" << left_gate_capacity << std::endl;
        debug_file << "Process: " << getpid() << std::endl;
    }
    if (expected_collisions > 0) {
        measuring_times.reserve(expected_collisions);
        total_left.reserve(expected_collisions);
    }
}

std::string Simulation::get_random_string(const std::size_t &length) {
    std::string random_string;
    const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> distribution(0, chars.size() - 1);
    for (std::size_t i = 0; i < length; i++) {
        random_string += chars[distribution(*rng)];
    }
    return random_string;
}

void Simulation::debug_write(const std::string &message) {
    if (debug) {
        char s[10];
        std::time_t now = std::time(nullptr);
        struct tm *p = localtime(&now);
        strftime(s, 10, "%H:%M:%S", p);
        debug_file << s << ":\t" << message << std::endl;
    }
}

void Simulation::reset_particle(const unsigned long &particle, const double &box_x_radius, const double &box_y_radius,
                                const unsigned long &direction) {
    px = 0;
    py = 0;
    while (not is_in_circle(px, py, direction) or is_in_gate(px, py, direction) or
           is_in_bridge(px, py)) {
        px = ((*unif_real)(*rng) - 0.5) * box_x_radius * 2;
        py = ((*unif_real)(*rng) - 0.5) * box_y_radius * 2;
    }
    directions.at(particle) = ((*unif_real)(*rng) - 0.5) * 2 * PI;
}

void Simulation::start(const double &left_ratio) {
    /**
     * Initiate all particles, ratio based on the method argument
     */
    time = 0;
    last_written_time = 0;
    in_left = 0;
    const double box_x_radius = circle_distance / 2 + circle_radius * 2;
    const double box_y_radius = circle_radius;
    if (bridge_height / 2 >= box_y_radius) {
        throw std::invalid_argument("Bride height too large; no initialization possible");
    }
    if (left_ratio * num_particles < 0 or left_ratio * num_particles > num_particles) {
        throw std::domain_error("Please choose ratio between 0 and 1");
    }
    if (distance_as_channel_length and not gate_is_flat) {
        throw std::domain_error("If the gate is not flat, the bridge correction should not be applied");
    }
    const auto num_left_particles = (unsigned long) (left_ratio * num_particles);
    for (unsigned long particle = 0; particle < num_left_particles; particle++) {
        reset_particle(particle, box_x_radius, box_y_radius, LEFT);
        compute_next_impact(particle);
        in_left++;
    }
    for (unsigned long particle = num_left_particles; particle < num_particles; particle++) {
        reset_particle(particle, box_x_radius, box_y_radius, RIGHT);
        compute_next_impact(particle);
    }
    sort_indices();
    measure();
}

void Simulation::update(const double &write_dt) {
    // Find next event: the first particle that has a new impact
    // If we really need more optimization, this is where to get it.
    unsigned long particle = sorted_indices.front();
    double next_impact = next_impact_times[particle];
    // Write a time slice, if desired
    if (write_dt > 0) {
        while (next_impact > last_written_time + write_dt) {
            write_positions_to_file(last_written_time + write_dt);
            last_written_time += write_dt;
        }
        printf("Writing position at %.2f\n", last_written_time);
    }

    // Update the data of the particle with the collision
    if (not is_in_domain(next_x_pos[particle], next_y_pos[particle])) {
        // printf("Stray particle %d about to leave domain at (%.5f,%.5f), re-entered\n", (int) particle, px, py);
        next_x_pos[particle] = sgn(next_x_pos) * (circle_distance / 2 + circle_radius);
        next_y_pos[particle] = 0;
    }

    // Process the location of the particle
    if (px > 0 and next_x_pos[particle] < 0) {
        in_left++;
    } else if (px < 0 and next_x_pos[particle] > 0) {
        in_left--;
    } else if (px == 0) {
        std::cout << "Exactly zero position (highly unlikely), so count is now off" << std::endl;
        // if this happens often (read: twice) you have a bug, otherwise, ignore
    }
    px = next_x_pos[particle];
    py = next_y_pos[particle];
    directions[particle] = next_directions[particle];
    impact_times[particle] = next_impact;
    time = next_impact;


    // Check if the particle explodes
    for (unsigned long direction = 0; direction < 2; direction++) {
        if (is_in_gate(px, py, direction) and is_going_in(particle)) {
            check_gate_admission(particle, direction);
        } else {
            check_gate_departure(particle, direction);
        }
    }

    // Find out when the next collision takes place
    compute_next_impact(particle);
    reindex_particle(particle, true);
    // Do something useful with this information
    measure();
}

void Simulation::sort_indices() {
    std::iota(sorted_indices.begin(), sorted_indices.end(), 0);

    sorted_indices.sort([this](size_t i1, size_t i2) {
        return next_impact_times[i1] < next_impact_times[i2];
    });
}

unsigned long Simulation::find_index(const unsigned long &particle) {
    auto it = std::find(sorted_indices.begin(), sorted_indices.end(), particle);
    if (it != sorted_indices.end()) {
        return std::distance(sorted_indices.begin(), it);
    } else {
        std::cout << "Lost " << particle << std::endl;
        throw std::invalid_argument("Particle not found?! New DS broken");
    }
}

void Simulation::insert_index(const unsigned long &particle) {
    const double &impact_time = next_impact_times[particle];
    unsigned long l = 0;
    unsigned long r = num_particles - 1;
    unsigned long m_old = 0;
    auto it = sorted_indices.begin();
    while (l < r) {
        unsigned long m = (l + r) / 2;
        std::advance(it, m - m_old);
        double m_time = next_impact_times[*it];
        m_old = m;
        if (m_time < impact_time) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    std::advance(it, l - m_old);
    sorted_indices.insert(it, particle);
}

void Simulation::reindex_particle(const unsigned long &particle, const bool &was_minimum) {
    if (was_minimum) {
        sorted_indices.pop_front();
    } else {
        sorted_indices.remove(particle);
    }
    insert_index(particle);
}

bool Simulation::is_in_gate(const double &x, const double &y, const unsigned long &direction) {
    if (gate_is_flat) {
        return ((int) direction * 2 - 1) * x >= 0 and std::fabs(x) <= bridge_length / 2;
    } else {
        return ((int) direction * 2 - 1) * x >= 0 and not is_in_circle(x, y, direction);
    }
}

bool Simulation::is_going_in(const unsigned long &particle) {
    return px * cos(directions[particle]) <= 0;
}

void Simulation::check_gate_admission(const unsigned long &particle, const unsigned long &direction) {
    if (gate_arrays[direction][particle] == 0) {
        // Not yet in gate, check admission
        if (gate_contents[direction].size() > gate_capacities[direction] - 1) {
            explode_gate(particle, direction);
        } else {
            gate_contents[direction].push_back(particle);
            gate_arrays[direction][particle] = 1;
        }
    }
}

void Simulation::check_gate_departure(const unsigned long &particle, const unsigned long &direction) {
    if (gate_arrays[direction][particle] == 1) {
        // Freshly leaving the gate
        gate_contents[direction].erase(std::remove(gate_contents[direction].begin(),
                                                   gate_contents[direction].end(), particle),
                                       gate_contents[direction].end());
        gate_arrays[direction][particle] = 0;
    }
}

void Simulation::explode_gate(const unsigned long &exp_particle, const unsigned long &direction) {
    do {
        directions[exp_particle] = get_retraction_angle(exp_particle);
        compute_next_impact(exp_particle);
    } while (not is_in_domain(next_x_pos[exp_particle], next_y_pos[exp_particle]));
    for (unsigned long particle: gate_contents[direction]) {
        double x, y;
        get_current_position(particle, x, y);
//            printf("Position updated to (%.3f, %.3f)\n", x, y);
        if (not is_in_domain(x, y)) {
            printf("Particle %d not in domain\n", (int) particle);
            debug_write("Particle " + std::to_string(particle) + " not found in domain");
        } else if (not is_in_gate(x, y, direction)) {
            // printf("Particle %d not in radius, distance from center: %.4f. Removed from gate list\n", (int) particle, sqrt(x * x + y * y));
            gate_contents[direction].erase(std::remove(gate_contents[direction].begin(),
                                                       gate_contents[direction].end(), particle),
                                           gate_contents[direction].end());
        }
        px = x;
        py = y;
        directions[particle] = get_retraction_angle(particle);
        impact_times[particle] = time;
        compute_next_impact(particle);
        reindex_particle(particle, false);
//            printf("After boom, we get new positions at time %.2f\n",next_impact_times(particle));
    }
}

void Simulation::measure() {
    measuring_times.push_back(time);
    total_left.push_back(in_left);
}

void Simulation::print_status() {
    printf("Time passed: %.2f\n", time);
    for (unsigned long particle = 0; particle < num_particles; particle++) {
        printf("Particle %d at \nPosition (%.4f, %.4f) at t=%.2f, angle %.2f pi\n", (int) particle, px, py,
               impact_times[particle], directions[particle] / PI);
        printf("Planned impact at\nPosition (%.4f, %.4f) at t=%.2f, angle %.2f pi\n",
               next_x_pos[particle],
               next_y_pos[particle], next_impact_times[particle], next_directions[particle] / PI);
    }
    printf("Particles left: %d, particles right: %d\n", (int) in_left, (int) (num_particles - in_left));
    printf("Particles in left gate: %d\t in right gate %d\n", (int) currently_in_left_gate.size(),
           (int) currently_in_right_gate.size());
}

void Simulation::write_positions_to_file(const double &time) {
    std::string filename = "results.dat";
    std::ofstream file;
    if (time == 0) {
        file.open(filename, std::ofstream::out | std::ofstream::trunc);
        file << "num_particles\tcircle_radius\tcircle_distance\tbridge_height\tbridge_size\n";
        file << num_particles << " " << circle_radius << " " << circle_distance << " "
             << bridge_height << " " << bridge_length << std::endl;
        file.close();
    }
    file.open(filename, std::ios_base::app);
    file << time << std::endl;
    for (unsigned long particle = 0; particle < num_particles; particle++) {
        file << px + (next_x_pos[particle] - px) * (impact_times[particle] - time) /
                     (impact_times[particle] - next_impact_times[particle]) << " ";
    }
    file << std::endl;
    for (unsigned long particle = 0; particle < num_particles; particle++) {
        file << py + (next_y_pos[particle] - py) * (impact_times[particle] - time) /
                     (impact_times[particle] - next_impact_times[particle]) << " ";
    }
    file << std::endl;
    for (unsigned long particle = 0; particle < num_particles; particle++) {
        file << directions[particle] << " ";
    }
    file << std::endl;
    file.close();
}

void Simulation::write_totals_to_file() {
    std::string filename = "totals.dat";
    std::ofstream file;
    file.open(filename, std::ofstream::out | std::ofstream::trunc);
    for (double m_time: measuring_times) {
        file << m_time << "\t";
    }
    file << std::endl;
    for (unsigned long left: total_left) {
        file << left << "\t";
    }
    file << std::endl;
    for (unsigned long left: total_left) {
        file << num_particles - left << "\t";
    }
    file << std::endl;
    file.close();
}

void Simulation::write_bounce_map_to_file(const unsigned long &particle) {
    std::string filename = "bounces.dat";
    std::ofstream file;
    file.open(filename, std::ios_base::app);
    file << px << " " << py << std::endl;
    file.close();
}

double Simulation::get_mass_spread() {
    return std::fabs(2. * total_left.back() - num_particles) / num_particles;
}

void Simulation::finish() {
    write_totals_to_file();
    debug_write("Finished at t=" + std::to_string(time) + " with " + std::to_string(total_left.size()) + " bounces");
    if (debug) {
        debug_file.close();
    }
}

void Simulation::couple_bridge() {
    /**
     * A priori, the bridge does not connect to the circles.
     * We need to make the bridge a little bit longer so the ends connect too.
     * We do this by computing the intersections between the bridge lines and the circle
     */
    const double discrepancy =
            2 * std::sqrt(std::pow(circle_radius, 2) - std::pow(bridge_height, 2) / 4) - 2 * circle_radius;
    if (distance_as_channel_length) {
        bridge_length = circle_distance;
        circle_distance = bridge_length + discrepancy;
    } else {
        bridge_length = circle_distance - discrepancy;
    }
}

bool Simulation::is_in_domain(const double &x, const double &y) {
    if (is_in_bridge(x, y)) {
        return true;
    } else {
        if (x < 0) {
            return is_in_circle(x, y, LEFT);
        } else {
            return is_in_circle(x, y, RIGHT);
        }
    }
}

bool Simulation::is_in_circle(const double &x, const double &y, const unsigned long &side) {
    if (side == LEFT) {
        return (x - left_center_x) * (x - left_center_x) + y * y < circle_radius * circle_radius;
    } else {
        return (x - right_center_x) * (x - right_center_x) + y * y < circle_radius * circle_radius;
    }
}


bool Simulation::is_in_bridge(const double &x, const double &y) {
/**
 * Note that these function is not mutually exclusive with left and right circle, and is not to be confused by `is_in_gate`.
 */
    return std::abs(x) <= bridge_length / 2 and std::abs(y) <= bridge_height / 2;
}

void Simulation::compute_next_impact(const unsigned long &particle) {
    /**
     * Start from some x, y, alpha.
     * Compute the location of boundary hit
     * Compute the time to next boundary hit
     * if boundary hit is gate hit:
     *  Flag this gate hit
     *
     *  How do we compute next boundary hit?
     *  We have 4 obstacles:
     *  Circle left, circle right, bridge, gate.
     *  Earliest impact count.
     *  If earliest impact is not a gate, then disregard this particle until it hits.
     *
     */
    double next_time = max_path;
    double next_angle = 0;
    double angle;
    double to_bridge = time_to_hit_bridge(particle, angle);
    // this flow is not supah dupah
    if (to_bridge < next_time) {
        next_time = to_bridge;
        next_angle = get_reflection_angle(directions[particle], angle);
    }
    double to_left = time_to_hit_circle(particle, left_center_x, angle);
    if (to_left < next_time) {
        next_time = to_left;
        next_angle = get_reflection_angle(directions[particle], angle);
    }
    double to_right = time_to_hit_circle(particle, right_center_x, angle);
    if (to_right < next_time) {
        next_time = to_right;
        next_angle = get_reflection_angle(directions[particle], angle);
    }
    double to_gate = time_to_hit_gate(particle);
    if (to_gate < next_time) {
        next_time = to_gate + EPS; // In the circle should be guaranteed in; out should be out
        next_angle = directions[particle];
//        if (is_in_gate_radius(px, py) and is_in_gate_radius(nx, ny)) {
//            printf("Small movement (%.3e) for particle %d detected\n", next_time, particle);
//        }
    }
    double to_middle = time_to_hit_middle(particle);
    if (to_middle < next_time) {
        next_time = to_middle + EPS;
        next_angle = directions[particle];
    }
    if (next_time == max_path) {
        reset_counter++;
        printf("Next time = maxpath =%.2f\nParticle has to be reset (%dth time)\n", next_time, reset_counter);
        printf("Position (%.4f, %.4f) at t=%.2f, angle %.2f pi\n", px, py, impact_times[particle],
               directions[particle] / PI);
        const double box_x_radius = circle_distance / 2 + circle_radius * 2;
        const double box_y_radius = circle_radius;
        const int direction = px > 0 ? RIGHT : LEFT;
        debug_write("Resetting particle " + std::to_string(particle));
        reset_particle(particle, box_x_radius, box_y_radius, direction);
        compute_next_impact(particle);
    } else {
        next_x_pos[particle] = px + next_time * cos(directions[particle]);
        next_y_pos[particle] = py + next_time * sin(directions[particle]);
        next_impact_times[particle] = time + next_time;
        next_directions[particle] = next_angle;
    }
}

void Simulation::get_current_position(const unsigned long &particle, double &x, double &y) {
    /**
     * Interpolate position at the current time. Returns in referenced variables
     */
    if (impact_times[particle] == next_impact_times[particle]) {
        x = px;
        y = py;
    } else {
        x = px + (next_x_pos[particle] - px) * (impact_times[particle] - time) /
                 (impact_times[particle] - next_impact_times[particle]);
        y = py + (next_y_pos[particle] - py) * (impact_times[particle] - time) /
                 (impact_times[particle] - next_impact_times[particle]);
    }
}

double Simulation::time_to_hit_bridge(const unsigned long &particle, double &normal_angle) {
    /**
     * Check if we hit the bottom line, and check if we hit the top line, and return a float.
     */
    //Recall: px=positions(particle,0), py=positions(particle,1)
    double rx = max_path * cos(directions[particle]);
    double ry = max_path * sin(directions[particle]);
    double sx = bridge_length;
    double sy = 0;
    // q_bottom = (left_x, bottom_y) and q_top = (left_x, top_y)
    // u = (q − p) × r / (r × s)
    const double denom = rx * sy - ry * sx;
    double u1 = ((-bridge_length / 2 - px) * ry - (-bridge_height / 2 - py) * rx) / denom;
    double u2 = ((-bridge_length / 2 - px) * ry - (bridge_height / 2 - py) * rx) / denom;
    // t = (q − p) × s / (r × s)
    double t1 = ((-bridge_length / 2 - px) * sy - (-bridge_height / 2 - py) * sx) / denom;
    double t2 = ((-bridge_length / 2 - px) * sy - (bridge_height / 2 - py) * sx) / denom;
    double min_t = 1;
    if (EPS < t1 and t1 < min_t and 0 <= u1 and u1 <= 1) {
        min_t = t1 - EPS;
        normal_angle = PI / 2;
    }
    if (EPS < t2 and t2 < min_t and 0 <= u2 and u2 <= 1) {
        min_t = t2 - EPS;
        normal_angle = -PI / 2;
    }
    return min_t * max_path;
}

void Simulation::circle_intersections(const unsigned &particle, const double &center_x, double &t1, double &t2) {
    double add_x = max_path * cos(directions[particle]);
    double add_y = max_path * sin(directions[particle]);
    const double t_pos_x = (px - center_x) / circle_radius;
    const double t_pos_y = (py - 0) / circle_radius;
    const double t_add_x = add_x / circle_radius;
    const double t_add_y = add_y / circle_radius;
    // Compose quadratic equation
    const double a = t_add_x * t_add_x + t_add_y * t_add_y;
    const double b = 2 * t_pos_x * t_add_x + 2 * t_pos_y * t_add_y;
    const double c = t_pos_x * t_pos_x + t_pos_y * t_pos_y - 1;
    const double D = b * b - 4 * a * c;
    if (D >= 0) {
        t1 = (-b - sqrt(D)) / (2 * a);
        t2 = (-b + sqrt(D)) / (2 * a);
    }
}

double Simulation::time_to_hit_circle(const unsigned long &particle, const double &center_x, double &normal_angle) {
    /**
     * Compute the time until next impact with one of the circle boundaries
     */
    double min_t = 1;
    double t1 = -1;
    double t2 = -1;
    double add_x = max_path * cos(directions[particle]);
    double add_y = max_path * sin(directions[particle]);
    circle_intersections(particle, center_x, t1, t2);
    // Find minimal root between 0 and 1 not in bridge
    double impact_x = 0;
    double impact_y = 0;
    if (EPS < t1 and t1 < min_t) {
        impact_x = px + t1 * add_x;
        impact_y = py + t1 * add_y;
        // Only hitting the circle if not in the bridge
        if (not is_in_bridge(impact_x, impact_y)) {
            normal_angle = atan2(0 - impact_y, center_x - impact_x);
            min_t = t1 - EPS;
        }
    }
    if (EPS < t2 and t2 < min_t) {
        impact_x = px + t2 * add_x;
        impact_y = py + t2 * add_y;
        // Only hitting the circle if not in the bridge
        if (not is_in_bridge(impact_x, impact_y)) {
            normal_angle = atan2(0 - impact_y, center_x - impact_x);
            min_t = t2 - EPS;
        }
    }
    return min_t * max_path;
}

double Simulation::get_reflection_angle(const double &angle_in, const double &normal_angle) {
    return fmod(2 * normal_angle - angle_in + PI, 2 * PI);
}

double Simulation::get_retraction_angle(const unsigned long &particle) {
    if (explosion_direction_is_random) {
        int side = sgn(px);
        return ((*unif_real)(*rng) - 0.5) * PI + PI / 2 * (1 - sgn(side));
    } else {
        if (cos(directions[particle]) * x_pos[particle] < 0) {
            return directions[particle] + PI;
        } else {
            return directions[particle];
        }
    }
}

double Simulation::time_to_hit_gate(const unsigned long &particle) {
    /**
     * Compute time towards the gate.
     * If the gate is circular: transform the domain and solve a quadratic equation.
     * If the gate is flat: Check the time until intersection with the vertical gate lines.
     * In order to ensure positivity of the solution, we use numerical rounding with epsilon for finding roots.
     */
    double min_path = max_path;
    if (gate_is_flat) {
        const double to_left_gate = (-bridge_length / 2 - px) / cos(directions[particle]);
        const double to_right_gate = (bridge_length / 2 - px) / cos(directions[particle]);
        if (to_left_gate > 0 and to_left_gate < min_path) {
            min_path = to_left_gate;
        }
        if (to_right_gate > 0 and to_right_gate < min_path) {
            min_path = to_right_gate;
        }
    } else {
        double min_t = 1;
        double t1 = -1;
        double t2 = -1;
        double add_x = max_path * cos(directions[particle]);
        double add_y = max_path * sin(directions[particle]);
        for (int direction = 0; direction < 2; direction++) {
            double center_x = left_center_x;
            if (direction == RIGHT) {
                center_x = right_center_x;
            }
            circle_intersections(particle, center_x, t1, t2);
            // Find minimal root between 0 and 1 in bridge
            double impact_x = 0;
            double impact_y = 0;
            if (EPS < t1 and t1 < min_t) {
                impact_x = px + t1 * add_x;
                impact_y = py + t1 * add_y;
                // Only hitting the circle if in the bridge
                if (is_in_bridge(impact_x, impact_y)) {
                    min_t = t1;
                }
            }
            if (EPS < t2 and t2 < min_t) {
                impact_x = px + t2 * add_x;
                impact_y = py + t2 * add_y;
                // Only hitting the circle if in the bridge
                if (is_in_bridge(impact_x, impact_y)) {
                    min_t = t2;
                }
            }
        }
        min_path = min_t * max_path;
    }
    return min_path;
}

double Simulation::time_to_hit_middle(const unsigned long &particle) {
    /**
     * Uses a line-line intersection algorithm (with identical nomenclature) from 
     * https://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect
     */
    double min_t = 1;
    double rx = max_path * cos(directions[particle]);
    double ry = max_path * sin(directions[particle]);
    double sx = 0;
    double sy = bridge_height;
    // u = (q − p) × r / (r × s)
    double denum = 1. / (rx * sy - ry * sx);
    double u = ((0 - px) * ry - (-bridge_height / 2 - py) * rx) * denum;
    // t = (q − p) × s / (r × s)
    double t = ((0 - px) * sy - (-bridge_height / 2 - py) * sx) * denum;
    if (EPS < t and t < min_t and 0 <= u and u <= 1) {
        min_t = t + EPS;
    }
    return min_t * max_path;
}

void Simulation::debug_is(std::vector<unsigned long> vec, std::list<unsigned long> list) {
    auto it = list.begin();
    bool same = true;
    for (unsigned long i = 0; i < vec.size(); i++) {
        if (*it != vec[i]) {
            std::cout << *it << " " << vec[i] << "   ";
            same = false;
        }
        it++;
    }
    if (!same) {
        printf("Arrays diverged\n");
    }
}
