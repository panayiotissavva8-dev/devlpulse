#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <ctime>
#include <cstdlib>
 
#include <crow.h>
#include <crow/middlewares/cookie_parser.h>
#include <sqlite3.h>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
 
#include "db/schema.h"
#include "utils/security.h"
#include "services/user_service.h"
#include "services/github_service.h"
#include "services/ws_manager.h"