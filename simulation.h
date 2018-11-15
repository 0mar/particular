//
// Created by Omar Richardson on 24/10/2018.
//

#ifndef BLOTZ_SIMULATION_H
#define BLOTZ_SIMULATION_H

#include <Eigen/Dense>
#include <iostream>
#include <fstream>
#include <random>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <algorithm>


class Simulation {
public:
    Simulation(int num_particles, double gate_radius);

    // Important parameters
    const int num_particles;
    double gate_radius;
    int gate_capacity;
    // Other parameters
    double circle_radius;
    double circle_distance;
    double bridge_height;
    // Computed quantities
    double left_center_x;
    double right_center_x;
    double max_path;
    double bridge_size;

    void setup();

    void start();

    bool is_in_domain(double x, double y);

    bool is_in_left_circle(double x, double y);

    bool is_in_right_circle(double x, double y);

    bool is_in_bridge(double x, double y);

    bool is_in_gate_radius(double x, double y);

    double time_to_hit_bridge(int particle, double &normal_angle);

    double time_to_hit_circle(int particle, double center_x, double &normal_angle);

    double time_to_hit_gate(int particle);

    double time;
    double last_written_time;
    Eigen::ArrayXd next_impact_times;
    Eigen::ArrayXd impact_times;
    Eigen::ArrayXXd next_positions;
    Eigen::ArrayXXd positions;
    Eigen::ArrayXd next_directions;
    Eigen::ArrayXd directions;
    Eigen::ArrayXi in_gate;
    Eigen::ArrayXi in_left;
    Eigen::ArrayXi in_right;

    std::vector<double> measuring_times;
    std::vector<int> total_left;
    std::vector<int> currently_in_gate;
    std::vector<int> total_right;

    void compute_next_impact(int particle);

    void get_current_position(int particle, double &x, double &y);

    void check_gate_explosion(int particle);

    void update(double write_dt);

    void measure();

    void print_status();

    void write_positions_to_file(double time);

    void write_totals_to_file();

    double get_reflection_angle(double angle_in, double normal_angle);

    double get_retraction_angle(int particle);

    void finish();

private:
    void couple_bridge();


    std::shared_ptr<std::random_device> rd;
    std::shared_ptr<std::mt19937> rng;
    std::shared_ptr<std::uniform_real_distribution<double>> unif_real;

};


#endif //BLOTZ_SIMULATION_H
