package com.testapp.logic;

/**
 * Abstract class demonstrating inheritance, template method pattern,
 * and abstract method polymorphism compatible across Java 8 through 25.
 */
public abstract class AbstractDataProcessor<T> {

    protected final String processorName;

    public AbstractDataProcessor(String processorName) {
        this.processorName = processorName;
    }

    public String getProcessorName() {
        return this.processorName;
    }

    public boolean validate(T input) {
        return input != null;
    }

    // Abstract method implemented by subclasses
    public abstract T process(T input);

    // Concrete template method coordinating validation and processing
    public T execute(T input) {
        if (!validate(input)) {
            throw new IllegalArgumentException("Invalid input for processor: " + processorName);
        }
        return process(input);
    }

    // Concrete subclass
    public static class TextSanitizerProcessor extends AbstractDataProcessor<String> {

        private final String prefix;

        public TextSanitizerProcessor(String prefix) {
            super("TextSanitizer");
            this.prefix = prefix;
        }

        @Override
        public boolean validate(String input) {
            return super.validate(input) && !input.isEmpty();
        }

        @Override
        public String process(String input) {
            return prefix + ":" + input.trim().toUpperCase();
        }
    }

    public static boolean testAbstractProcessor() {
        AbstractDataProcessor<String> processor = new TextSanitizerProcessor("CLEAN");
        if (!"TextSanitizer".equals(processor.getProcessorName())) {
            return false;
        }
        String result = processor.execute("   sample text   ");
        return "CLEAN:SAMPLE TEXT".equals(result);
    }
}
