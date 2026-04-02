#include "IMServer.h"

int main(int argc, char* argv[]) {
  const char* config_path = "config/server.conf";
  if (argc > 1) {
    config_path = argv[1];
  }
  IMServer server(config_path);
  server.start();

  return 0;
}
