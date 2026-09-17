package com.testapp.language;

/**
 * Java 17 language features showcase.
 * Features:
 * - Records (JEP 395)
 * - Sealed Classes and Interfaces (JEP 409)
 * - Pattern Matching for instanceof (JEP 394)
 * - Text Blocks (JEP 378)
 */
public class Java17 {

    // 1. Record
    public record UserRecord(String username, int age) {
        public boolean isAdult() {
            return age >= 18;
        }
    }

    // 2. Sealed Interface and Permitted Subclasses
    public sealed interface Shape permits Circle, Rectangle {
        double area();
    }

    public static final class Circle implements Shape {
        private final double radius;
        public Circle(double radius) { this.radius = radius; }
        @Override public double area() { return Math.PI * radius * radius; }
    }

    public static final class Rectangle implements Shape {
        private final double width, height;
        public Rectangle(double width, double height) { this.width = width; this.height = height; }
        @Override public double area() { return width * height; }
    }

    public static boolean test() {
        // 1. Test Record
        UserRecord user = new UserRecord("Alice", 25);
        if (!"Alice".equals(user.username()) || user.age() != 25 || !user.isAdult()) {
            return false;
        }

        // 2. Test Sealed Hierarchy
        Shape rect = new Rectangle(4.0, 5.0);
        if (rect.area() != 20.0) {
            return false;
        }

        // 3. Test Pattern Matching for instanceof
        Object obj = "Hello_Pattern_Matching_Java_17";
        if (obj instanceof String s) {
            if (!s.startsWith("Hello") || s.length() != 30) {
                return false;
            }
        } else {
            return false;
        }

        // 4. Test Text Blocks
        String jsonTextBlock = """
                {
                    "name": "Java17",
                    "status": "LTS"
                }
                """;
        if (!jsonTextBlock.contains("\"status\": \"LTS\"")) {
            return false;
        }

        return true;
    }
}
