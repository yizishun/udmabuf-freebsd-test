# udmabuf-freebsd-test

Test suite for `udmabuf` on FreeBSD using `atf` (Automated Testing Framework) and `kyua`.
it contains some simple udmabuf test and a selftest ported from linux

## Prerequisites

Ensure you have a C compiler and the `kyua` testing framework installed.

```sh
pkg install kyua
```

## Build Instructions

Navigate to the `tests` directory and run `make` to compile the test programs.

```sh
cd tests
make
```

## Running Tests

You can run the tests using `kyua`.

1.  **Run tests**:
    ```sh
    kyua test
    ```

2.  **View report**:
    ```sh
    kyua report
    ```

3.  **Debug a specific test case** (optional):
    If you need to debug a specific test case, you can run it directly:
    ```sh
    ./udmabuf_test -r <test_case_name>
    ```
    Or use `kyua debug`:
    ```sh
    kyua debug <test_program>:<test_case_name>
    ```

## Cleaning Up

To clean up the build artifacts:

```sh
make clean
```
