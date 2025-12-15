package client;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.ObjectInputStream;
import java.io.ObjectOutputStream;
import java.net.Socket;
import java.util.Scanner;
import javax.swing.*;

public class Client {
    public static void main(String[] args) {
        boolean authenticated = false;
        Scanner scanner = new Scanner(System.in);
        String user = null;
        boolean logOut = false;
        String operation = null;

        try {
            Socket socket = new Socket("localhost", 8080);
            System.out.println("Connection established");
            
            // object input & output stream
            // client and server should create in opposite order
            // if in server 'in' is defined first, create 'out' first in client, and flush it.
            // same if 'out' is first defined in server
            ObjectOutputStream out = new ObjectOutputStream(socket.getOutputStream());
            out.flush();
            ObjectInputStream in = new ObjectInputStream(socket.getInputStream());

            while(!authenticated) {
                
                System.out.print("\nEnter your username: ");
                user = scanner.nextLine();

                out.writeObject(user);
                String response = (String) in.readObject();

                if(response.equalsIgnoreCase("success")) {
                    authenticated = true;
                } else {
                    System.out.println(response);
                }
            }

            System.out.println("\nWelcome " + user + " !!");

            while(!logOut) {
                System.out.print("\nChoose operation you'd like to perform:\n"
                                + "1. look up total users and active users\n"
                                + "2. look up uploaded files\n"
                                + "3. look up public files of other users\n"
                                + "4. make a file request\n"
                                + "5. view unread messages\n"
                                + "6. upload a file\n"
                                + "7. view upload/download history\n"
                                + "8. log out\n"
                );

                operation = scanner.nextLine();
                Integer op = Integer.parseInt(operation);
                
                if( !(1 <= op && op <= 8)) {
                    System.out.println("Invalid operation");
                    continue;
                }

                switch(operation) {
                    case "1":
                        out.writeObject("1");
                        String res = (String) in.readObject();
                        System.out.println(res);
                        break;
                    
                    case "2":
                        out.writeObject("2");
                        
                        String files = (String) in.readObject();
                        System.out.println(files);

                        while(true) {
                            System.out.println("""
                                    \nChoose an option --
                                    1) Download a file
                                    2) cancel 
                                    """);
                            
                            String option = scanner.nextLine();
                            if(option.equalsIgnoreCase("1")) {

                                out.writeObject("download");
                                System.out.println("\nEnter name of the file you want to download --");
                                
                                String fileName = scanner.nextLine();
                                out.writeObject(fileName);

                                File file = new File("./client/" + fileName);
                                file.createNewFile();

                                BufferedOutputStream fileWriter = new BufferedOutputStream(new FileOutputStream(file));

                                while(true) {
                                    Integer readBytes = (Integer) in.readObject();

                                    if(readBytes == -1) {
                                        break;
                                    }

                                    byte[] chunk = (byte[]) in.readObject();
                                    fileWriter.write(chunk);

                                    out.writeObject("received");
                                }

                                String response = (String) in.readObject();
                                if(response.equalsIgnoreCase("file download complete")) {
                                    System.out.println("\n=== " + fileName + " === download complete");
                                } else if(response.equalsIgnoreCase("file download failed")) {
                                    System.out.println("\n=== " + fileName + " === download failed");
                                }

                                fileWriter.close();
                                
                                break;
                            } else if (option.equalsIgnoreCase("2")) {
                                out.writeObject("cancel");
                                break;
                            } else {
                                System.out.println("Invalid option. Choose again.");
                            }

                        }

                        break;

                    case "3":
                        out.writeObject("3");

                        System.out.println("\nname of user you want to look up:");
                        String userToLookUp = scanner.nextLine();

                        System.out.println("1.6");
                        
                        if(userToLookUp != null)
                            out.writeObject(userToLookUp);
                        
                        String response = (String) in.readObject();
                        if(response.equalsIgnoreCase("no user found")) {
                            System.out.println(response);
                            break;
                        } else {
                            response = (String) in.readObject();
                            System.out.println(response);
                        }

                        System.out.println("1");

                        while(true) {
                            System.out.println("""
                                    \nChoose an option --
                                    1) Download a file
                                    2) cancel 
                                    """);
                            
                            String option = scanner.nextLine();
                            if(option.equalsIgnoreCase("1")) {

                                out.writeObject("download");
                                System.out.println("\nEnter name of the file you want to download --");
                                
                                String fileName = scanner.nextLine();
                                out.writeObject(fileName);

                                File file = new File("./client/" + fileName);
                                file.createNewFile();

                                BufferedOutputStream fileWriter = new BufferedOutputStream(new FileOutputStream(file));

                                while(true) {
                                    Integer readBytes = (Integer) in.readObject();

                                    if(readBytes == -1) {
                                        break;
                                    }

                                    byte[] chunk = (byte[]) in.readObject();
                                    fileWriter.write(chunk);

                                    out.writeObject("received");
                                }

                                response = (String) in.readObject();
                                if(response.equalsIgnoreCase("file download complete")) {
                                    System.out.println("\n=== " + fileName + " === download complete");
                                } else if(response.equalsIgnoreCase("file download failed")) {
                                    System.out.println("\n=== " + fileName + " === download failed");
                                }

                                fileWriter.close();
                                
                                break;
                            } else if (option.equalsIgnoreCase("2")) {
                                out.writeObject("cancel");
                                break;
                            } else {
                                System.out.println("Invalid option. Choose again.");
                            }

                        }

                        break;
                    
                    case "4":
                        break;
                    
                    case "5":
                        break;
                    
                    case "6":
                        JFileChooser chooser = new JFileChooser();
                        int result = chooser.showOpenDialog(null);

                        if (result == JFileChooser.APPROVE_OPTION) {
                            out.writeObject("6");

                            File file = chooser.getSelectedFile();
                            System.out.println("You chose: " + file.getAbsolutePath());
                            
                            String fileNameSize = file.getName() + ", " + Long.toString(file.length());
                            out.writeObject(fileNameSize);

                            response = (String) in.readObject();

                            String fileID = null;
                            Integer uploadChunkSize = 0;
                            if(response.equalsIgnoreCase("success")) {
                                response = (String) in.readObject();
                                System.out.println("response " + response);
                                String[] responseSplit = response.split(",");

                                System.out.println("1.3");

                                fileID = responseSplit[0];
                                uploadChunkSize = Integer.parseInt(responseSplit[1].trim());
                                System.out.println("uploadChunkSize " + uploadChunkSize);
                            
                                BufferedInputStream fileRead = new BufferedInputStream(new FileInputStream(file));
                                while(true) {
                                    byte[] chunk = new byte[uploadChunkSize];
                                    Integer bytesRead = fileRead.read(chunk);
                                    
                                    out.writeObject(bytesRead);

                                    if(bytesRead == -1 ) {
                                        break;
                                    }
                                    
                                    out.writeObject(chunk);
                                    System.out.println("chunk " + chunk);

                                    String status = (String) in.readObject();
                                    if(status.equalsIgnoreCase("chunk upload success"))
                                        continue;
                                }

                                out.writeObject("file upload complete");
                                
                                response = (String) in.readObject();
                                System.out.println(response);

                                if(response.equalsIgnoreCase("file upload success")) {
                                    while(true) {
                                        System.out.println("""
                                                \nSave the file as ---
                                                1) private ( only you can access the file )
                                                2) public ( any user can access your file )
                                                
                                                Select option 1 or 2:
                                                """);

                                        response = scanner.nextLine();

                                        if(response.equalsIgnoreCase("1")) {
                                            out.writeObject("private");
                                            break;
                                        } else if(response.equalsIgnoreCase("2")) {
                                            out.writeObject("public");
                                            break;
                                        } else {
                                            System.out.println("Invalid option. choose again.");
                                        }
                                    }
                                }

                                fileRead.close();

                            } else {

                                response = (String) in.readObject();
                                System.out.println(response);
                            }
                            


                        }

                        break;
                    
                    case "7":
                        break;

                    case "8":
                        break;
                }


            }

            scanner.close();
            in.close();
            out.close();
            socket.close();
            
        } catch(ClassNotFoundException | IOException e) {
            System.out.println(e);
        }
    }
}