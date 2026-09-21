package com.testapp.api;

/**
 * Implementation of the kept API fixture. Freely renamed/shrunk except
 * where keep rules say otherwise; tracks invocations for verification.
 */
public class ImplementedAPI implements API {

    private static int calls = 0;

    @Override
    public void methodOne() {
        calls |= 1;
    }

    @Override
    public void methodTwo() {
        calls |= 2;
    }

    public static int getCalls() {
        return calls;
    }
}
