package com.testapp.language;

import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.function.BiFunction;
import java.util.function.Predicate;
import java.util.stream.Collectors;

/**
 * Java 11 language and library features showcase.
 * Features:
 * - Local-Variable Syntax for Lambda Parameters (JEP 323: (var x, var y) -> ...)
 * - String API enhancements: isBlank, strip, lines
 * - Predicate.not(...)
 * - Optional.isEmpty()
 */
public class Java11 {

    public static boolean test() {
        // 1. String.isBlank() and String.strip()
        String blankStr = "   \t\n  ";
        if (!blankStr.isBlank()) {
            return false;
        }

        String padded = "  hello world  ";
        if (!"hello world".equals(padded.strip())) {
            return false;
        }

        // 2. String.lines() stream
        String multiLine = "line1\nline2\nline3";
        List<String> lines = multiLine.lines().collect(Collectors.toList());
        if (lines.size() != 3 || !"line2".equals(lines.get(1))) {
            return false;
        }

        // 3. Predicate.not()
        List<String> items = Arrays.asList("apple", "", "banana", "   ", "cherry");
        List<String> nonBlank = items.stream()
                .filter(Predicate.not(String::isBlank))
                .collect(Collectors.toList());
        if (nonBlank.size() != 3) {
            return false;
        }

        // 4. Local-Variable Syntax for Lambda Parameters (var in lambdas)
        BiFunction<Integer, Integer, Integer> adder = (var x, var y) -> x + y;
        int sum = adder.apply(20, 22);
        if (sum != 42) {
            return false;
        }

        // 5. Optional.isEmpty()
        Optional<String> emptyOpt = Optional.empty();
        if (!emptyOpt.isEmpty()) {
            return false;
        }

        return true;
    }
}
