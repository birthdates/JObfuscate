package com.testapp;

import com.testapp.annotation.FeatureAudit;
import com.testapp.model.DataVault;
import com.testapp.model.Role;
import com.testapp.model.User;
import com.testapp.service.Auditable;
import com.testapp.service.CryptoService;
import com.testapp.service.PaymentGateway;
import com.testapp.service.StripePayment;
import com.testapp.logic.AutoCloseableSession;
import com.testapp.logic.FlowLogic;
import com.testapp.logic.ExternalCaller;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Supplier;
import java.lang.reflect.Method;
import com.testapp.logic.AbstractDataProcessor;
import com.testapp.api.API;
import com.testapp.api.ImplementedAPI;

public class Main {

    public static void main(String[] args) {
        long startMs = System.currentTimeMillis();
        
        System.out.println("==================================================");
        System.out.println("   Java Obfuscator Comprehensive Test Harness    ");
        System.out.println("==================================================");

        int passed = 0;
        int total = 14;

        // Test 1: String Literals & Constants Encryption
        System.out.print("[TEST 1/14] Testing String Literals & Secrets... ");
        String banner = CryptoService.getBanner();
        String masterKey = CryptoService.getMasterKey();
        String dbUrl = CryptoService.getDatabaseConnection();
        if (banner.contains("PROTECTED SYSTEM") 
            && masterKey.equals("AKIA-8921-TOP-SECRET-KEY-2025") 
            && dbUrl.contains("secure_vault")) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 2: Number Operations & Invariants
        System.out.print("[TEST 2/14] Testing Number Operations & Constants... ");
        int checkVal = CryptoService.computeCheckValue(100);
        byte[] scrambled = CryptoService.xorScramble(new byte[]{1, 2, 3}, 0x42);
        if (checkVal != 0 && scrambled.length == 3) {
            System.out.println("PASSED (CheckVal: " + checkVal + ")");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 3: Object Hierarchy, Overrides, & Inner Classes (Static Nested Builder, Member Inner Session)
        System.out.print("[TEST 3/14] Testing Object Hierarchy & Inner Classes... ");
        User u1 = new User.Builder()
                .id(101)
                .username("admin_user")
                .secretToken("token_xyz")
                .role(Role.ADMIN)
                .build();
        User u2 = new User(101, "admin_user", "different_token", Role.ADMIN);
        User.Session session = u1.createSession("sess-999");
        if (u1.equals(u2) 
            && u1.getRole().canAccessAdminPanel() 
            && u1.toString().contains("admin_user")
            && session.getSessionDetails().contains("admin_user")
            && session.getEnclosingUser() == u1) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 4: Interface Polymorphism, Default & Static Methods
        System.out.print("[TEST 4/14] Testing Interface Polymorphism & Default Methods... ");
        PaymentGateway gateway = new StripePayment("sk_test_123456789");
        boolean paid = gateway.processPayment(100.0, "USD");
        double fee = gateway.calculateFee(100.0);
        String auditSummary = gateway.generateAuditSummary(); // Interface default method
        long auditEpoch = Auditable.currentAuditEpoch();       // Interface static method
        API keptApi = API.get();                               // Kept-interface fixture
        keptApi.methodOne();
        keptApi.methodTwo();
        if (paid && Math.abs(fee - 3.20) < 0.01 
            && gateway.getProviderName().contains("Stripe")
            && auditSummary.contains("PAYMENT_GATEWAY")
            && auditEpoch > 0
            && keptApi != null && ImplementedAPI.getCalls() == 3) {
            System.out.println("PASSED (Fee: " + fee + ")");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 5: Try-With-Resources, Multi-Catch & Finally (AutoCloseableSession)
        System.out.print("[TEST 5/14] Testing Try-With-Resources & Multi-Catch... ");
        String successLog = AutoCloseableSession.runProtectedTransaction("SESSION_OK", 42);
        String caughtLog = AutoCloseableSession.runProtectedTransaction("SESSION_FAIL", -7);
        if (successLog.contains("[START]") && successLog.contains("OP_42_EXECUTED") && successLog.contains("[FINALLY]")
            && caughtLog.contains("[CAUGHT:IllegalArgumentException]") && caughtLog.contains("[FINALLY]")) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED! successLog=" + successLog + " | caughtLog=" + caughtLog);
        }

        // Test 6: Switches (Primitive, String, and Enum switch)
        System.out.print("[TEST 6/14] Testing Primitive, String, and Enum Switches... ");
        String algo = CryptoService.dispatchAlgorithmCode(1);
        int keySize = CryptoService.resolveCipherKeySize("RSA_4096");
        String perms = Role.getPermissions(Role.ADMIN);
        if ("AES_256_GCM".equals(algo) && keySize == 4096 && "ALL_PERMISSIONS".equals(perms)) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 7: Concurrency & Synchronization (Synchronized Method & Block)
        System.out.print("[TEST 7/14] Testing Concurrency & Synchronization... ");
        boolean tokenUpdated = u1.updateSecretToken("token_sync_active");
        u1.addTags("alpha", "beta", "gamma");
        List<String> tags = u1.getTags();
        if (tokenUpdated && tags.size() == 3 && tags.contains("beta")) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 8: Generics (Bounded Types, Generic Methods, Wildcards)
        System.out.print("[TEST 8/14] Testing Generics (Bounded, Methods, Wildcards)... ");
        DataVault<Integer> vault = new DataVault<Integer>();
        vault.store(10);
        vault.store(50);
        vault.store(30);
        int maxVal = vault.findMax();

        List<Number> numList = new ArrayList<Number>();
        DataVault.fillIntegers(numList, 4); // Lower-bounded wildcard
        double sum = DataVault.sumOfNumbers(numList); // Upper-bounded wildcard

        List<String> filtered = DataVault.filterItems(Arrays.asList("one", "two", "three"), s -> s.startsWith("t"));
        if (maxVal == 50 && sum == 10.0 && filtered.size() == 2) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 9: Lambdas, Functional Interfaces & Method References
        System.out.print("[TEST 9/14] Testing Lambdas & Method References... ");
        BiFunction<Integer, Integer, Integer> adder = Integer::sum;
        Function<String, String> upper = String::toUpperCase;
        Supplier<List<String>> listSupplier = ArrayList::new;

        List<String> createdList = listSupplier.get();
        createdList.add(upper.apply("hello"));
        int evenSum = FlowLogic.sumEvenNumbers(10); // 2 + 4 + 6 + 8 + 10 = 30
        if (adder.apply(10, 20) == 30 && "HELLO".equals(createdList.get(0)) && evenSum == 30) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 10: Streams API, Local Class & Anonymous Inner Class
        System.out.print("[TEST 10/14] Testing Streams, Local Classes & Anonymous Classes... ");
        List<String> streamOut = FlowLogic.processWithStreamsAndLambdas(Arrays.asList("gamma", "alpha", "beta"));
        List<Integer> sortedDesc = FlowLogic.sortAndValidateNumbers(Arrays.asList(15, -3, 42, 8));
        if (streamOut.equals(Arrays.asList("ALPHA", "BETA", "GAMMA"))
            && sortedDesc.equals(Arrays.asList(42, 15, 8))) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 11: Arrays (Multidimensional 2D Array, Array Cloning, Sorting, Binary Search)
        System.out.print("[TEST 11/14] Testing Multidimensional Arrays & Sorting... ");
        boolean arrayCheck = CryptoService.verifyMatrixAndSorting();
        int seq = FlowLogic.complexMathSequence(12, 10);
        long fib = FlowLogic.computeFibonacci(10);
        if (arrayCheck && seq > 0 && fib == 55) {
            System.out.println("PASSED (Fib(10): " + fib + ")");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 12: Runtime Annotations & Reflection Inspection
        System.out.print("[TEST 12/14] Testing Runtime Annotations & Reflection... ");
        FeatureAudit userAudit = User.class.getAnnotation(FeatureAudit.class);
        FeatureAudit roleAudit = Role.class.getAnnotation(FeatureAudit.class);
        if (userAudit != null && "UserModel".equals(userAudit.featureName()) && userAudit.priority() == 2
            && roleAudit != null && "RoleModel".equals(roleAudit.featureName())) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        // Test 13: External Callers, Reflection Proxies, & HTTP Web Request Response Validation
        System.out.print("[TEST 13/14] Testing External Callers & HTTP Web Request... ");
        String originalText = "Hello_Obfuscated_Enterprise_World_2025!";
        String encoded = ExternalCaller.encodeData(originalText);
        String decoded = ExternalCaller.decodeData(encoded);
        long sysTime = ExternalCaller.getSystemTime();
        boolean webValidated = ExternalCaller.sendAndValidateWebRequest();

        if (originalText.equals(decoded) && sysTime > 0 && webValidated) {
            System.out.println("PASSED (Web Request Verified)");
            passed++;
        } else {
            System.out.println("FAILED! (webValidated: " + webValidated + ")");
        }

        // Test 14: Abstract Classes & Template Method Pattern (Supported on Java 8–25)
        System.out.print("[TEST 14/14] Testing Abstract Classes & Template Method Pattern... ");
        boolean abstractSuccess = AbstractDataProcessor.testAbstractProcessor();
        if (abstractSuccess) {
            System.out.println("PASSED");
            passed++;
        } else {
            System.out.println("FAILED!");
        }

        System.out.println("==================================================");
        if (passed == total) {
            System.out.println("[OBFUSCATOR-VERIFICATION-PASS] ALL " + total + " BASELINE TEST SUITES PASSED SUCCESSFULLY!");
        } else {
            System.err.println("[OBFUSCATOR-VERIFICATION-FAIL] " + (total - passed) + " tests failed!");
            System.exit(1);
        }

        // Dynamic Version-Specific Language Features Showcase (Java 11, 17, 21, 25)
        int jvmVer = getJavaVersion();
        System.out.println("[INFO] Runtime Java Environment: Java " + jvmVer);

        if (jvmVer >= 11) {
            runLanguageFeatureTest("com.testapp.language.Java11", 11);
        }
        if (jvmVer >= 17) {
            runLanguageFeatureTest("com.testapp.language.Java17", 17);
        }
        if (jvmVer >= 21) {
            runLanguageFeatureTest("com.testapp.language.Java21", 21);
        }
        if (jvmVer >= 25) {
            runLanguageFeatureTest("com.testapp.language.Java25", 25);
        }

        long elapsed = System.currentTimeMillis() - startMs;
        System.out.println("==================================================");
        System.out.println("[FINAL-STATUS] ALL APPLICABLE LANGUAGE & OBFUSCATION SUITES COMPLETED IN " + elapsed + "ms.");
        System.exit(0);
    }

    private static void runLanguageFeatureTest(String className, int version) {
        try {
            Class<?> cls = Class.forName(className);
            Method testMethod = null;
            for (Method m : cls.getDeclaredMethods()) {
                if (java.lang.reflect.Modifier.isStatic(m.getModifiers()) &&
                    m.getParameterCount() == 0 &&
                    m.getReturnType() == boolean.class) {
                    testMethod = m;
                    testMethod.setAccessible(true);
                    break;
                }
            }
            if (testMethod == null) {
                System.err.println("[LANG-TEST] Java " + version + " (" + className + "): No public static boolean test() found!");
                System.exit(1);
            }
            boolean ok = (Boolean) testMethod.invoke(null);
            if (ok) {
                System.out.println("[LANG-TEST] Java " + version + " Language Features (" + className + ")... PASSED");
            } else {
                System.err.println("[LANG-TEST] Java " + version + " Language Features (" + className + ")... FAILED!");
                System.exit(1);
            }
        } catch (ClassNotFoundException e) {
            System.out.println("[LANG-TEST] Java " + version + " Language Features (" + className + ")... NOT PACKAGED (Skipping)");
        } catch (Exception e) {
            System.err.println("[LANG-TEST] Java " + version + " Language Features (" + className + ") threw exception: " + e);
            e.printStackTrace();
            System.exit(1);
        }
    }

    public static int getJavaVersion() {
        String version = System.getProperty("java.version");
        if (version == null) return 8;
        if (version.startsWith("1.")) {
            return Integer.parseInt(version.substring(2, 3));
        }
        int dot = version.indexOf('.');
        if (dot != -1) {
            try { return Integer.parseInt(version.substring(0, dot)); } catch (Exception ignored) {}
        }
        int dash = version.indexOf('-');
        if (dash != -1) {
            try { return Integer.parseInt(version.substring(0, dash)); } catch (Exception ignored) {}
        }
        try {
            return Integer.parseInt(version);
        } catch (NumberFormatException e) {
            return 8;
        }
    }
}
