#include <iostream>
#include <memory>
#include "simulation.h"
#include <string>

/**
 * This file contains an executable that can be used for efficient parameter regime explorations
 * of the two-chamber dynamics with two channels
 * It takes command line parameters that define the simulation, allowing for simple batch running
 * which allows for efficient parallel execution
 */

/**
 * Obtain the average mass spread and current as a function of the parameters below.
 * For a definition of the mass spread and the current, see simulation.h
 *
 * @param channel_length channel_length Length of the center channel
 * @param channel_width channel_width Width of the center channel
 * @param threshold Number of particles that can at the same time in the channel
 * @param radius Radius of the chamber
 * @param second_length Length of the back channel
 * @param second_width Width of the back channel
 * @param num_particles Number of particles in the system
 * @param left_ratio Initial ratio of number of particles in the left chamber
 * @param M_t Transient time, measured in number of collisions
 * @param M_f Final time, measured in number of collisions
 * @param av_chi Average mass spread, return value
 * @param currents Average currents counted per instance, return value
 */
void get_mass_spread(double channel_length, double channel_width, int threshold, double radius, double second_length,
             double second_width, int num_particles, double left_ratio, unsigned long M_t, unsigned long M_f,
             double &av_chi, std::vector<double> &currents) {
    Simulation sim = Simulation(num_particles, channel_width, radius, channel_length, threshold, threshold);
    sim.gate_is_flat = true;
    sim.distance_as_channel_length = true;
    sim.expected_collisions = M_f;
    sim.second_length = second_length;
    sim.second_width = second_width;
    sim.setup();
    std::ostringstream s;
    av_chi = 0;
    try {
        sim.start(left_ratio);
    } catch (const std::invalid_argument &ex) {
        printf("Not running for bridge width %.2f and radius %.2f, returning 0\n", channel_width, radius);
    }

    while (sim.num_collisions < M_t) {
        sim.update(0.0);
    }
    const double weight = 1. / (double) (M_f - M_t);
    const std::vector<int> count_offset = sim.current_counters;
    const double time_offset = sim.time;
    while (sim.num_collisions < M_f) {
        sim.update(0.0);
        av_chi += weight * sim.get_mass_spread();
    }
    currents.resize(4);
    for (unsigned int i = 0; i < 4; i++) {
        currents.at(i) = (sim.current_counters.at(i) - count_offset.at(i)) / (sim.time - time_offset);
    }
}

/**
 * Obtain the average mass spread and current as a function of the parameters below,
 * and write its value 500 times during the simulation.
 * For a definition of the mass spread and the current, see simulation.h
 *
 * @param channel_length channel_length Length of the center channel
 * @param channel_width channel_width Width of the center channel
 * @param threshold Number of particles that can at the same time in the channel
 * @param radius Radius of the chamber
 * @param second_length Length of the back channel
 * @param second_width Width of the back channel
 * @param num_particles Number of particles in the system
 * @param left_ratio Initial ratio of number of particles in the left chamber
 * @param M_t Transient time, measured in number of collisions
 * @param M_f Final time, measured in number of collisions
 * @param id File identifier to write auxiliary results to.
 * @param av_chi Average mass spread, return value
 * @param current Average current, return value
 */
void get_mass_spread_evo(double channel_length, double channel_width, double radius, int threshold, double second_length,
                 double second_width, int num_particles, double left_ratio, unsigned long M_t, unsigned long M_f,
                 const std::string &id, double &av_chi, double &current) {
    av_chi = 0;
    current = 0;
    const int num_points = 500;
    const unsigned long step_size = M_f / num_points;
    Simulation sim = Simulation(num_particles, channel_width, radius, channel_length, threshold, threshold);
    sim.gate_is_flat = true;
    sim.distance_as_channel_length = true;
    sim.second_length = second_length;
    sim.second_width = second_width;
    sim.setup();
    std::random_device rd;
    std::mt19937 re(rd());
    std::uniform_real_distribution<double> unif(0.5, 1);
    std::ostringstream s;
    try {
        sim.start(left_ratio);
        sim.write_positions_to_file(0);
    } catch (const std::invalid_argument &ex) {
        printf("Not running for bridge width %.2f and radius %.2f, returning 0\n", channel_width, radius);
    }
    double dt = 0;
    while (sim.num_collisions < M_f) {
//        if (sim.measuring_times.size() ==M_t) {
//            dt = 0.025;
//            sim.last_written_time = sim.time;
//        }
//        if (sim.measuring_times.size() == M_t + 10000){
//            dt = 0;
//        }
        sim.update(dt);
        if (sim.num_collisions % step_size == 0) {
            s << sim.num_collisions << "," << sim.time << "," << sim.in_left << "," << std::fabs(sim.get_mass_spread())
              << std::endl;
        }
    }
    std::ofstream result_file(id + ".chi", std::ios::app);
    result_file << s.str();
    result_file.close();
}

/**
 * Find the thermalisation times for a specific set of parameters. Executable, takes command line values.
 *
 * @param argc number of command line arguments
 * @param argv list of command line arguments
 */
void mass_spread_and_current_for(int argc, char *argv[]) {
    const int num_arguments = 11;
    if (argc != num_arguments + 1) {
        std::cout << "Printing arguments: " << argc << std::endl;
        for (unsigned int i = 0; i < argc; i++) {
            std::cout << argv[i] << " ";
        }
        std::cout << std::endl;
        throw std::invalid_argument(
                "Please provide (in order) (1) channel length, (2) channel width, (3) threshold, (4) urn radius,"
                " (5) second channel length, (6) second channel width, (7) number of particles, (8) initial ratio,"
                " (9) transient time, (10) final time, (11) identifier");
    }
    const double channel_length = std::stod(argv[1]);
    const double channel_width = std::stod(argv[2]);
    const int threshold = std::stoi(argv[3]);
    const double radius = std::stod(argv[4]);
    const double second_length = std::stod(argv[5]);
    const double second_width = std::stod(argv[6]);
    const int num_particles = std::stoi(argv[7]);
    const double initial_ratio = std::stod(argv[8]);
    const int M_t = std::stoi(argv[9]);
    const int M_f = std::stoi(argv[10]);
    const std::string file_id = argv[11];
    const std::string sim_id = argv[12];
    double av_chi = 0;
    std::vector<double> currents;
    get_mass_spread(channel_length, channel_width, threshold, radius, second_length, second_width, num_particles,
            initial_ratio, M_t, M_f, av_chi, currents);
    std::ostringstream s;
    s << sim_id << "," << av_chi;
    for (unsigned int i = 0; i < 4; i++) {
        s << ", " << currents.at(i);
    }
    s << std::endl;
    std::ofstream result_file(file_id + ".out", std::ios::app);
    result_file << s.str();
    result_file.close();
}

int main(int argc, char *argv[]) {
    mass_spread_and_current_for(argc, argv);
    return 0;
}

