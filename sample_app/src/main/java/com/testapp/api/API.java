package com.testapp.api;

/**
 * Kept-interface fixture for keep-rule tests.
 * The interface itself can be kept while the implementation scrambles:
 * member type references must follow the rename, kept member names stay.
 */
public interface API {
    void methodOne();
    void methodTwo();

    ImplementedAPI api = new ImplementedAPI();

    static API get() {
        return api;
    }
}
