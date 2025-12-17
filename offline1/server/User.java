package server;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;

public class User {
    public boolean activeStatus = false;
    public BufferedWriter unreadPersonalMessagesWriter = null;
    
    User(String userName) {
        try {
            this.unreadPersonalMessagesWriter = new BufferedWriter(new FileWriter("./userData/" + userName + "/unreadPersonalMessages.txt", true));
        } catch (IOException e) {
            System.out.println(e);
        }
    }
}
