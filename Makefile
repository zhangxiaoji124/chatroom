# Chatroom - C++20 WebSocket Chat Server
# Build with mingw32-make -j4
# Produces build/chatroom_server.exe

CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -Iinclude -Ithird_party
LDFLAGS  := -lws2_32 -lpthread
TARGET   := build/chatroom_server_v2.exe
TESTTGT  := build/chatroom_tests.exe
APPTGT   := build/ChatroomApp.exe
ADMINTGT := build/ChatroomAdminApp.exe
.DEFAULT_GOAL := all

SRCS := src/main.cpp src/server.cpp src/ai_client.cpp src/user_manager.cpp src/ws_handler.cpp src/storage.cpp src/rate_limiter.cpp src/cluster_manager.cpp
OBJS := $(SRCS:src/%.cpp=build/%.o)
SQLITEOBJ := build/sqlite3.o

TESTSRCS := tests/unit_tests.cpp src/user_manager.cpp src/ws_handler.cpp src/storage.cpp src/rate_limiter.cpp
TESTOBJS := build/unit_tests.o build/user_manager.o build/ws_handler.o build/storage.o build/rate_limiter.o $(SQLITEOBJ)

# 手动把 src 下的测试依赖源编译成 build/*.o
build/user_manager.o: src/user_manager.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/ws_handler.o: src/ws_handler.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/storage.o: src/storage.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/rate_limiter.o: src/rate_limiter.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/cluster_manager.o: src/cluster_manager.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SQLITEOBJ): third_party/sqlite3.c | build
	gcc -O2 -c $< -o $@

.PHONY: all app admin clean test run e2e

all: $(TARGET) $(APPTGT) $(ADMINTGT)

app: $(APPTGT)

admin: $(ADMINTGT)

build:
	@mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: tests/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(SQLITEOBJ)
	$(CXX) $(OBJS) $(SQLITEOBJ) $(LDFLAGS) -o $(TARGET)

$(APPTGT): src/launcher.cpp | build
	$(CXX) -std=c++20 -O2 -mwindows src/launcher.cpp -lws2_32 -lshell32 -o $(APPTGT)

$(ADMINTGT): src/admin_launcher.cpp | build
	$(CXX) -std=c++20 -O2 -mwindows src/admin_launcher.cpp -lws2_32 -lshell32 -o $(ADMINTGT)

$(TESTTGT): $(TESTOBJS)
	$(CXX) $(TESTOBJS) $(LDFLAGS) -o $(TESTTGT)

clean:
	@rm -rf build

test: build
	mingw32-make $(TESTTGT)
	.\$(TESTTGT)

e2e: $(TARGET)
	python tests/e2e_test.py

run: $(TARGET)
	.\$(TARGET)
