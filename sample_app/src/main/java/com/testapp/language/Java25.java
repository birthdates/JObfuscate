package com.testapp.language;

import java.util.List;
import java.util.Map;

/**
 * Modern Java 22+ / 25 language features showcase.
 * Features:
 * - Unnamed Variables & Patterns (JEP 456: standard feature in modern Java)
 * - Math.clamp (standard since Java 21)
 * - Modern pattern matching and string handling
 */
public class Java25 {

    public record Item(String id, int score) {}

    public static boolean test() {
        // 1. Unnamed Variables (JEP 456) in exception handlers and loops
        boolean catchWorked = false;
        try {
            Integer.parseInt("not_a_number");
        } catch (NumberFormatException _) {
            // Unnamed variable '_'
            catchWorked = true;
        }
        if (!catchWorked) {
            return false;
        }

        // 2. Unnamed Variables in lambda parameters and pattern matching
        Map<String, Integer> map = Map.of("a", 1, "b", 2);
        int count = 0;
        for (var _ : map.keySet()) {
            count++;
        }
        if (count != 2) {
            return false;
        }

        // 3. Unnamed Pattern in record deconstruction
        Object itemObj = new Item("ITEM_001", 99);
        boolean scoreMatched = false;
        if (itemObj instanceof Item(_, int score)) {
            if (score == 99) {
                scoreMatched = true;
            }
        }
        if (!scoreMatched) {
            return false;
        }

        // 4. Math.clamp standard utility
        int clamped = Math.clamp(150, 0, 100);
        if (clamped != 100) {
            return false;
        }

        return true;
    }
}
