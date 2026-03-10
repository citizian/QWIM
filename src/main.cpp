#include "IMServer.h"

int main() {
  IMServer server("config/server.conf");
  server.start();

  return 0;
}
