import java.io.DataOutputStream;
import java.io.FileOutputStream;
import java.lang.reflect.Field;

/* Export the user's original MathHelper initialized table, not a substitute. */
public class MathTableExport {
    public static void main(String[] args) throws Exception {
        Class<?> source = Class.forName("net.minecraft.src.MathHelper");
        Field field = source.getDeclaredField("SIN_TABLE");
        field.setAccessible(true);
        float[] table = (float[]) field.get(null);
        try (DataOutputStream out = new DataOutputStream(new FileOutputStream(args[0]))) {
            for (float value : table) out.writeInt(Integer.reverseBytes(Float.floatToRawIntBits(value)));
        }
    }
}
