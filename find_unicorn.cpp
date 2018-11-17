#include <iostream>
#include <memory>
#include "simulation.h"
#include <string>
#include <fstream>


void write_results(std::string &id, std::vector<double> &data) {
    std::ofstream results_file;
    results_file.open(id + ".txt");
    for (double datum: data) {
        results_file << datum << "\t";
    }
    results_file << std::endl;
}

void many_particle_animation() {
    printf("Running the animation for 200 particle\n");
    Simulation simulation = Simulation(200, 0.7);
    simulation.left_gate_capacity = 15;
    simulation.right_gate_capacity = 2;
    simulation.bridge_height = 0.5;
    simulation.setup();
    simulation.start();
    simulation.write_positions_to_file(0);
    double dt = 0.025;
    while (simulation.time < 100) {
        simulation.update(dt);
    }
}

double get_cool_down_time(int number_of_particles, int gate_capacity) {
    Simulation simulation = Simulation(number_of_particles, 0.3);
    simulation.left_gate_capacity = gate_capacity;
    simulation.right_gate_capacity = 0;
    simulation.setup();
    simulation.start();
    //simulation.write_positions_to_file(0);
    while (simulation.in_right.sum() < 10 and simulation.time < 1E5) {
        simulation.update(0.0);
    }
    return simulation.time;
}

double test_parameters(int number_of_particles, int gate_capacity) {
    int repeats = 100;
    double total_time = 0;
    for (int i = 0; i < repeats; i++) {
        total_time += get_cool_down_time(number_of_particles, gate_capacity);
    }
    return total_time / repeats;
}

int main(int argc, char *argv[]) {
    int mode = 0;
    if (argc == 2) {
        mode = std::stoi(argv[1]);
    } else if (argc == 3) {
        int number_of_particles = std::stoi(argv[1]);
        int gate_capacity = std::stoi(argv[2]);
        //printf("Assuming a gate radius of %.2f and a gate capacity of %d\n", gate_radius, gate_capacity);
        std::cout << test_parameters(number_of_particles, gate_capacity) << std::endl;
        return 0;
    }
    switch (mode) {
        case 1: {
            break;
        }
        case 2: {
            break;
        }
        default: {
            std::cout << get_cool_down_time(200, 2) << std::endl;
            break;
        }
    }
    return 0;
}

