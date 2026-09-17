package com.testapp.language;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Java 21 language and runtime features showcase.
 * Features:
 * - Sequenced Collections (JEP 431: getFirst, getLast, reversed)
 * - Record Patterns (JEP 440: Point(int x, int y))
 * - Pattern Matching for switch (JEP 441)
 * - Virtual Threads (JEP 444: Thread.ofVirtual())
 */
public class Java21 {

    public record Coordinate(int x, int y) {}

    public static boolean test() {
        // 1. Sequenced Collections
        List<String> list = new ArrayList<>();
        list.add("first");
        list.add("middle");
        list.add("last");

        if (!"first".equals(list.getFirst()) || !"last".equals(list.getLast())) {
            return false;
        }
        List<String> reversed = list.reversed();
        if (!"last".equals(reversed.getFirst())) {
            return false;
        }

        // 2. Record Patterns
        Object pointObj = new Coordinate(10, 20);
        boolean recordPatternMatched = false;
        if (pointObj instanceof Coordinate(int px, int py)) {
            if (px == 10 && py == 20) {
                recordPatternMatched = true;
            }
        }
        if (!recordPatternMatched) {
            return false;
        }

        // 3. Pattern Matching for switch
        String switchResult = formatObject(new Coordinate(100, 200));
        if (!"Coord:100,200".equals(switchResult)) {
            return false;
        }

        String strResult = formatObject("test_switch");
        if (!"String:test_switch".equals(strResult)) {
            return false;
        }

        // 4. Virtual Threads
        AtomicBoolean threadRan = new AtomicBoolean(false);
        try {
            Thread vThread = Thread.ofVirtual().start(() -> {
                threadRan.set(true);
            });
            vThread.join();
        } catch (InterruptedException e) {
            return false;
        }

        return threadRan.get();
    }

    private static String formatObject(Object obj) {
        return switch (obj) {
            case Integer i -> "Integer:" + i;
            case String s -> "String:" + s;
            case Coordinate(int x, int y) -> "Coord:" + x + "," + y;
            case null -> "Null";
            default -> "Unknown";
        };
    }
}
