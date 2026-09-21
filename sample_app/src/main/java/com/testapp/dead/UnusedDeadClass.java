package com.testapp.dead;

/**
 * Completely dead and unreferenced class.
 * Used to verify tree-shaking / dead code elimination in JObfuscate.
 */
@Deprecated
public class UnusedDeadClass {

    private String deadToken;
    public int deadCounter;

    public UnusedDeadClass() {
        this.deadToken = "NEVER_USED_TOKEN_VALUE";
        this.deadCounter = 9999;
    }

    public String getDeadToken() {
        return deadToken;
    }

    public static int deadStaticCalculation(int x) {
        return x * 42;
    }
}
