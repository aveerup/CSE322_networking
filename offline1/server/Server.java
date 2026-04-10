package server;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.HashMap;
// import java.util.Scanner;
import java.util.Map;
import java.io.*;

class Server{

    private static void serverConfig(ServerConfig serverConfig){
        try {
            BufferedReader configIn = new BufferedReader(new FileReader("./server/serverConfig.txt"));

            String line = null;
            String[] lines = null;
            while((line = configIn.readLine()) != null) {
                System.out.println(line);
                lines = line.split(",");

                if(lines[0].trim().equalsIgnoreCase("max_buffer_size")) {
                    serverConfig.MAX_BUFFER_SIZE = Long.parseLong(lines[1].trim());
                } else if(lines[0].trim().equalsIgnoreCase("max_chunk_size")) {
                    serverConfig.MAX_CHUNK_SIZE = Integer.parseInt(lines[1].trim());
                    System.out.println("1.4");
                } else if(lines[0].trim().equalsIgnoreCase("min_chunk_size")) {
                    serverConfig.MIN_CHUNK_SIZE = Integer.parseInt(lines[1].trim());
                } else if(lines[0].trim().equalsIgnoreCase("curr_buffer_size")) {
                    serverConfig.CURR_BUFFER_SIZE = Long.parseLong(lines[1].trim());
                } else if(lines[0].trim().equalsIgnoreCase("max_user_num")) {
                    serverConfig.MAX_USER_NUM = Integer.parseInt(lines[1].trim());
                } else if(lines[0].trim().equalsIgnoreCase("curr_user_num")) {
                    serverConfig.CURR_USER_NUM = Integer.parseInt(lines[1].trim());
                }

            }
            
            configIn.close();

        } catch(IOException e) {
            System.out.println(e);
        }
    }

    public static void main(String[] args){
        
        int port = 8080; // Default port
        boolean stopServer = false; // Stop the server when becomes true
        Map<String, Boolean> users = new HashMap<>();
        Map<String, String> files = new HashMap<>();
        Map<String, User> userMessageWriter = new HashMap<>();
        
        ServerConfig serverConfig = new ServerConfig();

        serverConfig(serverConfig);

        try {
            File userList = new File("./userList.txt");
            File userData = new File("./userData");
            
            if(!userList.exists()) {
                userList.createNewFile();
            }

            if(!userData.exists()) {
                userData.mkdir();
            }

            BufferedReader userListReader = new BufferedReader(new FileReader("./userList.txt"));        
            BufferedWriter userListWriter = new BufferedWriter(new FileWriter("./userList.txt", true));
            
            String line = null;
            
            while((line = userListReader.readLine()) != null) {
                users.put(line, false);
                userMessageWriter.put(line, new User(line));
            }

            ServerSocket serverSocket = new ServerSocket(port);
            System.out.println("\nServer is listening on port " + port);

            while (!stopServer) {
                // System.out.println("Write 'terminate' to stop server");

                Socket socket = serverSocket.accept();
                System.out.println("\nNew client connected");

                Session session = new Session(socket, 
                                            userListWriter, 
                                            users,
                                            files,
                                            serverConfig,
                                            userMessageWriter);
                session.start();
            }
            
            serverSocket.close();
            userListReader.close();
            userListWriter.close();

        } catch (IOException e) {
            System.out.println("Error occurred: " + e.getMessage());
        }
    }
}