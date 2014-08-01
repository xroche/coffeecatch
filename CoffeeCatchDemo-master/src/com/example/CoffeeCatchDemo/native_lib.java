package com.example.CoffeeCatchDemo;

/**
 * Test native lib
 */
public class native_lib {

    static {
        System.loadLibrary("native");
    }

    /**
     * error function
     * @param a unused
     * @return error code (0 for SUCCESS)
     */
    public native int native_func(float a);
}
