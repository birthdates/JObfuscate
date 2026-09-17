package com.testapp.logic;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

public class FlowLogic {

    public static int complexMathSequence(int start, int rounds) {
        int val = start;
        for (int i = 0; i < rounds; i++) {
            if (val % 2 == 0) {
                val = (val / 2) + 3;
            } else {
                val = (val * 3) + 1;
            }

            if (val > 1000) {
                val -= 500;
            }
        }
        return val;
    }

    public static String evaluateTier(int score) {
        switch (score / 10) {
            case 10:
            case 9:
                return "DIAMOND";
            case 8:
                return "PLATINUM";
            case 7:
                return "GOLD";
            case 6:
                return "SILVER";
            default:
                return "BRONZE";
        }
    }

    public static long computeFibonacci(int n) {
        if (n <= 0) return 0;
        if (n == 1) return 1;
        long a = 0;
        long b = 1;
        int count = 2;
        while (count <= n) {
            long temp = a + b;
            a = b;
            b = temp;
            count++;
        }
        return b;
    }

    // Streams, Lambdas & Method references
    public static List<String> processWithStreamsAndLambdas(List<String> items) {
        Predicate<String> notEmpty = s -> s != null && !s.trim().isEmpty();
        Function<String, String> toUpper = String::toUpperCase; // Method reference (arbitrary object)
        Supplier<List<String>> listSupplier = ArrayList::new;    // Constructor reference

        return items.stream()
                .filter(notEmpty)
                .map(toUpper)
                .sorted()
                .collect(Collectors.toCollection(listSupplier));
    }

    // IntStream, reduce, and static method reference
    public static int sumEvenNumbers(int maxInclusive) {
        BiFunction<Integer, Integer, Integer> adder = Integer::sum; // Static method reference
        return IntStream.rangeClosed(1, maxInclusive)
                .filter(n -> n % 2 == 0)
                .reduce(0, adder::apply);
    }

    // Anonymous inner class and Local class declared inside method
    public static List<Integer> sortAndValidateNumbers(List<Integer> numbers) {
        // Local class declared inside a method
        class LocalPositiveValidator {
            boolean isPositive(Integer n) {
                return n != null && n > 0;
            }
        }
        LocalPositiveValidator validator = new LocalPositiveValidator();
        List<Integer> filtered = new ArrayList<Integer>();
        for (Integer num : numbers) {
            if (validator.isPositive(num)) {
                filtered.add(num);
            }
        }

        // Anonymous inner class implementing Comparator
        Collections.sort(filtered, new Comparator<Integer>() {
            @Override
            public int compare(Integer o1, Integer o2) {
                // Descending order
                return o2.compareTo(o1);
            }
        });

        return filtered;
    }
}
