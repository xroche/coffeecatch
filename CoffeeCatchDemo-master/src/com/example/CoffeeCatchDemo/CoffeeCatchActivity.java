package com.example.CoffeeCatchDemo;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;

public class CoffeeCatchActivity extends Activity {

    private Button testButton;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);

        bindControls();
        initControls();
    }

    private void bindControls() {
        testButton = (Button) findViewById(R.id.Main_test);
    }

    private void initControls() {
        testButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                test();
            }
        });
    }

    private void test() {
        native_lib lib = new native_lib();

        try {
            int result = lib.native_func(10);
            Toast.makeText(this, "invoked with code " + result, Toast.LENGTH_LONG).show();
        } catch (Throwable t) {
            t.printStackTrace();
            Toast.makeText(this, "crash catched", Toast.LENGTH_LONG).show();
        }
    }
}
