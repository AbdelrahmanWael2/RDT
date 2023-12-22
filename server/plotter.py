import matplotlib.pyplot as plt

# Read values from the file
with open('/home/abdelrahman/Documents/PS/RDT/server/cwnd.txt', 'r') as file:
    lines = file.readlines()

# Convert lines to a list of integers (assuming the values are integers)
values = [int(line.strip()) for line in lines]

# Create a simple plot
plt.plot(values, marker='o')

# Add labels and title
plt.xlabel('Time')
plt.ylabel('Congestion Window Size')
plt.title('Plot of Values from File')

# Display the plot
plt.show()
