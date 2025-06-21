#include "openterface/cli.hpp"

int main(int argc, char **argv) {
    openterface::CLI cli;
    return cli.run(argc, argv);
}