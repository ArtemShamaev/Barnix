package java.awt;

import java.awt.image.BufferStrategy;

public class Canvas extends Component {
    private BufferStrategy strategy;
    public Canvas() {}
    public void createBufferStrategy(int n) { strategy = new BufferStrategy(); }
    public BufferStrategy getBufferStrategy() { return strategy; }
    public void paint(Graphics g) {}
    public void update(Graphics g) { paint(g); }
}
