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
        
        ServerConfig serverConfig = new ServerConfig();

        serverConfig(serverConfig);

        Long MAX_BUFFER_SIZE = serverConfig.MAX_BUFFER_SIZE;
        Integer MAX_CHUNK_SIZE = serverConfig.MAX_CHUNK_SIZE;
        Integer MIN_CHUNK_SIZE = serverConfig.MIN_CHUNK_SIZE;
        Long CURR_BUFFER_SIZE = serverConfig.CURR_BUFFER_SIZE;
        final Object serverConfigLock = new Object();

        try {
            File userList = new File("./userList.txt");
            File userData = new File("./userData");
            File fileList = new File("./fileList.txt");
            
            if(!userList.exists()) {
                userList.createNewFile();
            }

            if(!userData.exists()) {
                userData.mkdir();
            }

            if(!fileList.exists()) {
                fileList.createNewFile();
            }

            BufferedReader userListReader = new BufferedReader(new FileReader("./userList.txt"));        
            BufferedWriter userListWriter = new BufferedWriter(new FileWriter("./userList.txt", true));
            
            BufferedReader fileListReader = new BufferedReader(new FileReader("./fileList.txt"));
            BufferedWriter fileListWriter = new BufferedWriter(new FileWriter("./fileList.txt"));

            String line = null;
            
            while((line = userListReader.readLine()) != null) {
                users.put(line, false);
            }

            while((line = fileListReader.readLine()) != null) {
                String[] fileIdName = line.split(",");

                files.put(fileIdName[0], fileIdName[1]);
            }

            ServerSocket serverSocket = new ServerSocket(port);
            System.out.println("Server is listening on port " + port);

            while (!stopServer) {
                // System.out.println("Write 'terminate' to stop server");

                Socket socket = serverSocket.accept();
                System.out.println("New client connected");

                Session session = new Session(socket, 
                                            userListWriter, 
                                            users,
                                            files,
                                            MAX_BUFFER_SIZE,
                                            MAX_CHUNK_SIZE,
                                            MIN_CHUNK_SIZE,
                                            CURR_BUFFER_SIZE);
                session.start();
            }
            
            serverSocket.close();
            userListReader.close();
            userListWriter.close();
            fileListReader.close();
            fileListWriter.close();

        } catch (IOException e) {
            System.out.println("Error occurred: " + e.getMessage());
        }
    }
}