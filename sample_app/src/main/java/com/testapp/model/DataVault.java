package com.testapp.model;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Predicate;

// Generic class with bounded type parameter
public class DataVault<T extends Comparable<T>> {
    private final List<T> items = new ArrayList<T>();

    public void store(T item) {
        if (item != null) {
            items.add(item);
        }
    }

    public T findMax() {
        if (items.isEmpty()) return null;
        T max = items.get(0);
        for (T item : items) {
            if (item.compareTo(max) > 0) {
                max = item;
            }
        }
        return max;
    }

    public int size() {
        return items.size();
    }

    // Generic method with type parameter
    public static <E> List<E> filterItems(List<E> input, Predicate<E> predicate) {
        List<E> result = new ArrayList<E>();
        for (E item : input) {
            if (predicate.test(item)) {
                result.add(item);
            }
        }
        return result;
    }

    // Upper-bounded wildcard (? extends Number)
    public static double sumOfNumbers(List<? extends Number> numbers) {
        double sum = 0.0;
        for (Number num : numbers) {
            sum += num.doubleValue();
        }
        return sum;
    }

    // Lower-bounded wildcard (? super Integer)
    public static void fillIntegers(List<? super Integer> target, int count) {
        for (int i = 1; i <= count; i++) {
            target.add(i);
        }
    }
}
