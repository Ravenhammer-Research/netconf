# Test Suite for FreeBSD Network Configuration Tool

This directory contains the test suite for the networking tool using FreeBSD's Kyua testing framework.

## Test Structure

```
tests/
├── Atffile                 # Main test suite configuration
├── unit/                   # Unit tests
│   ├── Atffile            # Unit test configuration
│   └── test_commands.c    # Command parsing test helper
├── integration/           # Integration tests
│   └── Atffile            # Integration test configuration
└── README.md              # This file
```

## Test Categories

### Unit Tests (`tests/unit/`)
- **Command Parsing**: Tests the command parser functionality
- **Invalid Commands**: Tests error handling for malformed commands
- **Data Structures**: Tests the command and configuration structures

### Integration Tests (`tests/integration/`)
- **Client-Server Communication**: Tests basic client-server interaction
- **Interface Configuration**: Tests interface setup and modification
- **Route Configuration**: Tests route addition and deletion
- **FIB Functionality**: Tests multiple routing table support

## Running Tests

### Using Kyua Directly
```bash
# Build the application and tests
make all

# Run all tests
kyua test

# Run specific test categories
kyua test unit/
kyua test integration/

# Run specific test cases
kyua test unit/test_command_parsing
kyua test integration/test_interface_configuration
```

### Test Results
```bash
# View test results
kyua report

# View detailed test results
kyua report --verbose
```

## Test Requirements

### System Requirements
- FreeBSD system with Kyua installed
- Root privileges for integration tests
- Network interfaces available for testing

### Dependencies
```bash
# Install Kyua if not already installed
pkg install kyua-atf
```

### Kernel Configuration
For FIB testing, ensure multiple routing tables are configured:
```bash
# Check current FIB count
sysctl net.fibs

# Configure multiple FIBs in /boot/loader.conf
echo "net.fibs=4" >> /boot/loader.conf
```

## Test Safety

### Safe Tests
- Command parsing tests
- Client-server communication tests
- Read-only operations (show commands)

### Potentially Destructive Tests
- Interface configuration tests (may modify network interfaces)
- Route configuration tests (may modify routing table)
- FIB tests (may modify multiple routing tables)

### Test Isolation
- Tests use loopback interface (`lo0`) for safe testing
- Tests clean up after themselves
- Tests check for required privileges before running

## Adding New Tests

### Unit Tests
1. Add test case to `tests/unit/Atffile`
2. Create test helper if needed
3. Update Makefile if new binaries are required

### Integration Tests
1. Add test case to `tests/integration/Atffile`
2. Ensure proper cleanup in test body
3. Add appropriate timeouts and error handling

### Test Helper Programs
1. Create helper in appropriate test directory
2. Add build target to Makefile
3. Update test runner script if needed

## Troubleshooting

### Common Issues
- **Permission denied**: Run tests with `sudo`
- **Interface not found**: Use `lo0` for safe testing
- **FIB not available**: Skip FIB tests or configure multiple FIBs
- **Server won't start**: Check if socket file exists, remove if needed

### Debug Mode
```bash
# Run tests with debug output
kyua test --verbose

# Run specific test with debug
kyua test unit/test_command_parsing --verbose
```

## Continuous Integration

The test suite is designed to work with CI/CD systems:
- Tests can run in containers
- Exit codes indicate test success/failure
- Tests are isolated and clean up after themselves
- Tests check for required system state before running 