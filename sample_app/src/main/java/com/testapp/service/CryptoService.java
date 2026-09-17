package com.testapp.service;

import java.util.Arrays;

public class CryptoService {
    // Secret strings that MUST be encrypted by the obfuscator
    private static final String SECRET_MASTER_KEY = "AKIA-8921-TOP-SECRET-KEY-2025";
    private static final String DATABASE_CONNECTION = "jdbc:postgresql://db.production.internal:5432/secure_vault";
    private static final String BANNER_TEXT = "=== PROTECTED SYSTEM ACCESS ONLY ===";

    // Number constants to test number obfuscation
    private static final int MAGIC_HEADER = 0x5A4D; // 23117
    private static final int HASH_PRIME = 31;
    private static final int MASK = 0x7FFFFFFF;

    public static String getBanner() {
        return BANNER_TEXT;
    }

    public static String getMasterKey() {
        return SECRET_MASTER_KEY;
    }

    public static String getDatabaseConnection() {
        return DATABASE_CONNECTION;
    }

    public static byte[] xorScramble(byte[] input, int key) {
        byte[] result = new byte[input.length];
        for (int i = 0; i < input.length; i++) {
            result[i] = (byte) ((input[i] ^ (key & 0xFF)) + 1);
        }
        return result;
    }

    public static int computeCheckValue(int seed) {
        int acc = seed;
        acc = (acc * HASH_PRIME) ^ MAGIC_HEADER;
        acc = acc & MASK;
        return acc;
    }

    // Switch on primitive int
    public static String dispatchAlgorithmCode(int code) {
        switch (code) {
            case 1:
                return "AES_256_GCM";
            case 2:
                return "CHACHA20_POLY1305";
            case 3:
                return "RSA_4096";
            default:
                return "UNKNOWN_CIPHER";
        }
    }

    // Switch on String
    public static int resolveCipherKeySize(String cipherName) {
        if (cipherName == null) return -1;
        switch (cipherName) {
            case "AES_256_GCM":
                return 256;
            case "CHACHA20_POLY1305":
                return 256;
            case "RSA_4096":
                return 4096;
            case "DES_EDE3":
                return 168;
            default:
                return 128;
        }
    }

    // Multidimensional array, array cloning, Arrays.sort and Arrays.binarySearch
    public static boolean verifyMatrixAndSorting() {
        // Multidimensional array (multianewarray)
        int[][] matrix = new int[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                matrix[i][j] = (i + 1) * (j + 1);
            }
        }
        if (matrix[2][2] != 9) return false;

        // Array cloning
        int[] original = new int[]{42, 12, 88, 3, 99, 1};
        int[] cloned = original.clone();
        if (cloned.length != original.length) return false;

        // Array sorting & binary search
        Arrays.sort(cloned);
        int idx = Arrays.binarySearch(cloned, 42);
        return idx >= 0 && cloned[idx] == 42;
    }
}
